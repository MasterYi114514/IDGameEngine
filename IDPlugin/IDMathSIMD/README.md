# ID.Math.SIMD 使用说明（消费者版）

`ID.Math.SIMD` 是 IDGameEngine 的 SIMD 数学插件（backend = SIMD）。它是**纯头插件**
（`kind = interface` + `lifecycle = none`）：没有服务槽、没有运行期函数表、没有生命周期回调，
能力全部是编译期契约与公开头里的内联实现。因此它**不占** `ID::ServiceSlot::Math`，也不需要
`get_service`；消费者只需要正确的 include 路径与编译期身份宏。

本文面向**使用**它的下游开发者：身份、ISA 前提、布局契约（含 padding）、向量与矩阵的接口、
失败语义、精度口径、集成前提与明确禁止的用法。实现内部结构不在本文范围。

- 当前接口修订：`ID::Math::SIMD::kMathSimdApiRevision = 1`
- 插件名 / 能力名：`ID.Math.SIMD`；CMake target：`IDMathSIMD`；命名空间：`ID::Math::SIMD`
- 编译期身份宏：`ID_USE_PLUGIN_IDMATHSIMD`（由组装层的 PUBLIC usage requirement 注入，**不是**本插件头定义的）
- 唯一对外伞头：`IDMathSIMDApi.hpp`
- 基线 ISA：**x64 SSE2**（不满足时**编译期**失败，见第 2 节）
- 当前交付范围：`vector<T, N>` 的存储/访问/运算、`matrix<Rows, Cols>` 的存储/访问/运算、
  `pack` / `unpack`（见第 5、6、7 节）

---

## 1. 身份与形态

| 项 | 取值 |
|---|---|
| `plugin.json` | `schema_version = 1`、`kind = interface`、`lifecycle = none`、`version = 1` |
| `provides` | 只有 `capability = ID.Math.SIMD` + `api_revision = 1`，**没有** `slot` |
| 目录内容 | `plugin.json`、`CMakeLists.txt`（只有一次 `id_assembly_add_module(IDMathSIMD)`）、`include/`、本 README；**没有** `src/` |
| 运行期符号 | 无。没有 DLL 服务表，也没有 `on_register` / `on_init` / `on_shutdown` / `on_unload` |

`ID.Math.Scalar` 与 `ID.Math.SIMD` 是**两个独立插件、两个独立 target、两个命名空间**：同名类型
（`vec3` 等）互不相同，二者互不依赖、互不 `using`、不共享头文件，也不存在"第三个共享 Math 插件"。
两个伞头名字不同（`IDMathScalarApi.hpp` / `IDMathSIMDApi.hpp`），同时被选中时 include 路径不会撞名。

## 2. ISA 基线：x64 SSE2（编译期强制）

1. 只有在 **x86_64 + SSE2 可用**时才能编译本 backend 的公开头。判据位于
  `include/detail/IDMathSIMDIsaGate.hpp`，按编译器族分支（MSVC 用 `_M_X64` / `_M_AMD64`，
  GCC/Clang 用 `__x86_64__` / `__amd64__` 与 `__SSE2__`），不成立时**预处理期 `#error`**：
  - 非 x86_64（例如 `g++ -m32`）：`ID.Math.SIMD 的基线 ISA 是 x64 SSE2：当前目标不是 x86_64，拒绝编译…`
  - 关闭 SSE2（例如 `g++ -mno-sse2`）：`ID.Math.SIMD 需要 SSE2：当前编译配置没有启用 SSE2…`
2. **没有运行期 ISA 调度**：不做 `cpuid` 之类的探测、没有按 ISA 分派的函数指针表、没有降级到标量
   路径的分支。ISA 是编译期前提；运行期不检查、不冷启动校验。
3. **AVX2/FMA 不是本 backend 的 variant**：按 D0-02，AVX2/FMA 只作为后续专用 variant 提案，当前未冻结。
   本 backend 既不要求、也不检测 `__AVX2__` / `__FMA__`，不为它预留宏开关。用 `-mavx2 -mfma` /
   `/arch:AVX2` 编译是允许的（SSE2 是子集），本 backend 只承诺 SSE2 基线语义；已按这组 flag 实跑验证（第 10 节）。
4. **ISA flag 由构建层统一传播**（`cmake/IDFlags.cmake`）；插件不写 `target_compile_options` /
   `target_link_options`，消费者也不要为它手写 ISA 选项。
5. 产品口径：**同时选中 Scalar 与 SIMD 的产品整体按 SIMD 的 ISA 基线发布**；只有"仅 Scalar"的产品
   才承诺更低基线。未适配的组合在 configure / 编译期直接失败，不静默降级。

## 3. 向量布局契约（含 padding）

唯一数据载体是 `T storage[kStorageCount]`，逻辑分量数是 `N`。整个类型 `alignas(16)`（128-bit lane），
存储槽位规则为：`N <= 2` 时 `kStorageCount = N`，否则向上取整到 4 的倍数。

实测值（MinGW GCC 16.2.0 x64 与 MSVC 19.52 x64 两侧一致）：

| 类型 | `kStorageCount` | `kPaddingLaneCount` | `sizeof` | `alignof` | `storage_bytes()` | `data_len()` |
|---|---|---|---|---|---|---|
| `vec2` = `vector<float, 2>` | 2 | 0 | **16** | 16 | **8** | 2 |
| `vec3` = `vector<float, 3>` | 4 | 1 | **16** | 16 | **16** | 3 |
| `vec4` = `vector<float, 4>` | 4 | 0 | **16** | 16 | **16** | 4 |
| `vector<double, 4>` | 4 | 0 | 32 | 16 | 32 | 4 |
| `vector<int, 3>` | 4 | 1 | 16 | 16 | 16 | 3 |

**padding 语义（公开契约，逐条明确）**：

1. **声明出来的 padding lane** 是 `storage[N .. kStorageCount-1]`，共 `kPaddingLaneCount` 个
   （只有 `vec3` 有 1 个）。
2. **参与构造与赋值**：四个构造入口（默认构造、逐分量构造、跨类型转换、`splat`）**无条件**把它们写成
   `+0.0f`（**位模式全零**），不读取任何源值；隐式拷贝/移动构造与赋值按数组成员逐槽复制，因此由上述
   入口产生的对象在复制后 padding lane 仍为零（用 `memcpy` 把外来字节灌进对象表示会原样带过去，
   那属于契约外用法）。零值只是**确定性**，不是"可以把 padding lane 当 0 参与运算"的许可——运算必须
   用掩码，掩码与加载不得越界（D0-02）。
3. **不属于公开值**：padding lane 不是运算输入、不是运算结果、不参与 `dot` / `length` / `normalize`
   等聚合；逻辑 `N` 之外的 lane 都不属于公开值。
4. **不可读**：`data()` 的可读长度是 `data_len()`（= `N`）；`operator[]` 的前提是 `index < N`。
   即使 padding lane 物理可寻址（`vec3` 的 `data()` 后 4 字节），读取它属于契约外用法。
5. **运算不改变布局**：所有运算入口都返回新的 `vector`，`sizeof` / `alignof` / `storage_bytes()` /
   `data_len()` 与构造出来的对象完全相同；运算结果的 padding lane 被**固定写 `+0.0f`**
   （先按位掩码清零再写回），因此结果对象与构造入口产生的对象在 padding 上一致。
6. **ABI 尾部填充**：当 `kStorageCount * sizeof(T)` 不是 16 的倍数时（典型是 `vec2`：存储 8 字节、
   类型对齐 16 字节），`sizeof(vector)` 会向上取整到 16 的倍数，多出来的 **8 字节属于 ABI 尾部填充**：
   不属于任何成员、**不可读**、**不保证清零**，拷贝时是否复制由 ABI 决定，代码不得依赖。
   本 backend 的算术**不会对这 8 字节发 16 字节访问**：`vec2` 的载入是 4 字节 `movss` 成对或
   8 字节 `movsd`，写回是 4 字节 `movss` 或 8 字节 `movlps`/`movsd`（都 ≤ 8 字节，机器码已抽查）。
   按值返回 / 赋值时编译器可能按对象大小整块拷 16 字节，那是 ABI 的拷贝语义，不是运算读了填充。
7. `storage_bytes()` 是**存储载体的字节数**（`kStorageCount * sizeof(T)`），因此可能小于
   `sizeof(vector)`（`vec2` 就是 8 vs 16）。`data_len()` 是 `data()` 的可读长度（= `N`）。
   **不要用 `sizeof` 反推布局**，也不要假设与 `ID.Math.Scalar` 的同名类型同大小/同对齐
   （`Scalar` 的 `vec3` 是 12 字节紧凑布局，`SIMD` 的 `vec3` 是 16 字节且多 1 个 padding lane）。

上述取值由公开头末尾的 `static_assert` 钉住（`sizeof` / `alignof` / `kStorageCount` /
`trivially copyable` / `standard layout` / 默认构造 `noexcept`），改动布局必须先改这些断言。

## 4. 向量：构造与访问

```cpp
#include "IDMathSIMDApi.hpp"     // 唯一伞头；include 路径由 target 的 PUBLIC usage requirement 传播

using ID::Math::SIMD::vec3;

vec3 a;                          // 默认构造：全部槽位零初始化（含 padding lane）
vec3 b{1.0f, 2.0f, 3.0f};        // 逐分量构造：实参个数必须恰好等于 N，且是 explicit
vec3 c = vec3::splat(2.0f);      // 单值填充：3 个逻辑分量都写 2，padding lane 仍为零

b.x() = 5.0f;                    // x()/y()/z()/w() 是 storage 的具名视图，按 N 收窄
b[1] = 6.0f;                     // 无越界检查；前提 index < N
const float* p = b.data();       // 16 字节对齐；可读长度 = b.data_len() == 3
```

- **默认构造零初始化**（D0-03）。这是契约，不是"零成本/无额外开销"的结论；不提供 `uninitialized` tag。
- 逐分量构造是 `explicit`，实参个数不等于 `N` 时不参与重载决议（少给/多给都是编译期错误，不补齐）。
- 跨类型转换必须 `explicit`（`vector<double,3>` → `vec3` 要显式构造），不做隐式提升/降级。
- 所有成员函数 `noexcept`；类型满足 `trivially copyable` 与 `standard layout`（可安全按值传递、
  可用 `memcpy` 复制），拷贝/赋值是隐式生成的成员语义（整数组复制，含 padding lane）。
- `data()` 返回 16 字节对齐的指针（对象本身 `alignas(16)`，栈/成员/`new` 都满足）。

## 5. 向量运算（`T == float`，`N ∈ {2,3,4}`）

只对 `T == float` 且 `N ∈ {2,3,4}` 存在；`vector<int, 3>` / `vector<float, 8>` 的**存储**可用，
但这些运算**不存在**（调用即编译失败，没有标量 fallback，也不隐式提升到 `double`）。

| 入口 | 说明 |
|---|---|
| `operator+` / `operator-` | 逐分量加 / 减 |
| `operator*(v, scalar)` / `operator*(scalar, v)` / `operator/(v, scalar)` | 标量乘 / 除（真除法，不用倒数近似）；**没有** `operator*(vector, vector)` |
| `hadamard(lhs, rhs)` | 逐分量积（与 `dot` 不同名，避免歧义） |
| `dot(lhs, rhs)` | 内积；按 128-bit lane **成对**归约：`(l0+l2)+(l1+l3)` |
| `squared_length(v)` / `length(v)` | 长度平方 / 欧几里得长度 |
| `cross(lhs, rhs)` | **仅** `vec3`：三维叉积 |
| `cross2(lhs, rhs)` | **仅** `vec2`：二维叉积，返回标量，**不复用**三维语义 |
| `normalize_unchecked(v)` | 前提：各分量有限且 `squared_length(v) > 0`；不做检查 |
| `try_normalize(v, out)` | checked；失败返回 `false` 且**不改写 `out`** |
| `equals(lhs, rhs)` | 逐分量精确相等（`==` 语义） |
| `near_equal(lhs, rhs, abs_tolerance, rel_tolerance)` | 逐分量双容差比较，判定式：`|a-b| <= abs + rel * max(|a|,|b|)` |
| `pack(v, out)` / `unpack(src, out)` | 见第 7 节 |

```cpp
using namespace ID::Math::SIMD;

const vec3 a{1.0f, 2.0f, 3.0f};
const vec3 b{4.0f, 5.0f, 6.0f};
const vec3 sum  = a + b;                       // (5, 7, 9)
const vec3 half = a * 0.5f;                    // (0.5, 1, 1.5)
const float d   = dot(a, b);                   // 32
const vec3 n    = normalize_unchecked(a);      // 单位向量
const vec3 c    = cross(a, b);                 // (-3, 6, -3)
const bool eq   = equals(a, a) && !equals(a, b);
const bool near = near_equal(a, half * 2.0f, 1e-6f, 1e-5f);

vec3 out;
if (!try_normalize(vec3{}, out))                // 零向量：返回 false，out 不变
{
    // 失败分支：out 仍是调用前的值
}
```

**语义与边界**：

1. `try_normalize` 的非有限值分类用**位模式前检**，不依赖 `isfinite`，因此不会被
   `-ffast-math` / `/fp:fast` 消掉。失败情形：任一分量非有限、`squared_length` 非有限
   （含"分量有限但平方溢出到 `inf`"）、`squared_length` 不为正。失败时 `out` **逐字节不变**；
   `&out == &v` 安全。
2. `normalize_unchecked` 不做检查：零向量得到 `inf` / `NaN` 分量属于调用方违约。
3. `dot` 的归约顺序与 `ID.Math.Scalar` 的升序累加不同，两者结果可能差 ULP 量级：精度按
   double oracle + 容差判定，**不承诺**与另一个 backend bitwise 一致（D0-05）。
4. 默认运算**不带 FMA、不带 `approx_rsqrt`**：`length` / `try_normalize` / `normalize_unchecked`
   走 `std::sqrt` 与其倒数，不加近似入口；需要更高精度的场景不要依赖 fast-math 下的倒数值。
5. **`/fp:fast` / `-ffast-math` 的边界**：这类选项允许编译器假定"不存在 `NaN`"，并可能把 `x == x`
   折成恒真（MSVC `/fp:fast` 实测会在 `vec2` / `vec3` 实例化上这样做）。因此 `equals` /
   `near_equal` 的 **`NaN` 判定在 fast-math 配置下不构成契约**；需要"是不是非有限值"只能用
   `try_normalize` 这类位模式前检入口。有限输入之间的比较结果不受该边界影响。
6. 运算入口**不是 `constexpr`**（intrinsic 不是常量表达式）；存储层与访问面仍是 `constexpr`。
   这是可求值性的差异，不是语义或精度差异。

## 6. 矩阵：`matrix<Rows, Cols>`（元素固定 `float`）

**列主序 + 列向量**是两条独立规则：元素偏移恒为 `offset(r, c) = c * ColumnStride + r`；
`matrix * vector` 按列向量的线性组合解释。`Rows` / `Cols` 只能取 `2 / 3 / 4`（9 种形状，含矩形）。

**列 stride 与列 padding（公开布局）**：`ColumnStride` 固定为 **4 个 float 槽位 = 16 字节**，
整个矩阵 `alignas(16)`：

| 形状 | `kColumnStride` | 每列逻辑元素 | **每列 padding 槽位 / 字节** | 矩阵总 padding | `sizeof` | `alignof` |
|---|---|---|---|---|---|---|
| `matrix<2, C>` | 4 | 2 | **2 个槽位 / 8 字节** | `C * 8` | `C * 16` | 16 |
| `matrix<3, C>` | 4 | 3 | **1 个槽位 / 4 字节** | `C * 4` | `C * 16` | 16 |
| `matrix<4, C>` | 4 | 4 | **0**（`ColumnStride == Rows`） | 0 | `C * 16` | 16 |

实测：`sizeof(matrix<2,2>) = 32`、`<2,3> = 48`、`<2,4> = 64`、`<3,2> = 32`、`<3,3> = 48`、
`<3,4> = 64`、`<4,2> = 32`、`<4,3> = 48`、`<4,4> = 64`；全部 `alignof = 16`。
`sizeof` 恒为 16 的整数倍，因此矩阵**没有** ABI 尾部填充。

**不可读边界**：`data()` 返回列主序定长元素块的起点，连续可寻址槽位数是
`Cols * ColumnStride`（= `storage_bytes() / sizeof(float)`），但**只有 `offset(r, c)` 且 `r < Rows`
的位置承载公开值**；每列尾部 `ColumnStride - Rows` 个槽位是 padding：**不可读**、不参与运算、
由构造入口与所有返回新矩阵的运算固定写 `+0.0f`。`data()` **不是**紧凑的 `Rows × Cols` 数组，
不能当 row-contiguous `span` 遍历，也不能拿它做跨 backend 重解释。

```cpp
using namespace ID::Math::SIMD;

matrix<3, 3> m = matrix<3, 3>::identity();   // identity 必须显式调用；默认构造是零矩阵
m.get_element(0, 1) = 2.0f;                  // 性能路径：返回 float&，偏移 c * 4 + r
m[1][2] = -1.0f;                             // 语法糖（RowProxy）；不承诺零开销

const matrix<3, 3> a = matrix<3, 3>::identity();
const vec3 v{1.0f, 2.0f, 3.0f};
const vec3 transformed = a * v;              // matrix<R,C> * vector<float,C> -> vector<float,R>
const matrix<3, 3> square = a * m;           // matrix<R,K> * matrix<K,C> -> matrix<R,C>
const matrix<3, 3> flipped = transpose(m);   // matrix<R,C> -> matrix<C,R>

const float det = determinant(m);            // 仅方阵；奇异时为 0
matrix<3, 3> inverse;
if (!try_inverse(m, inverse))                // 近奇异 / 非有限输入：返回 false，inverse 不变
{
    // 失败分支：inverse 仍是调用前的值
}
```

**运算与语义**：

| 入口 | 约束与说明 |
|---|---|
| `matrix<R,K> * matrix<K,C>` | 结果 `matrix<R,C>`；每个输出列 = `Σ_k A(k) * B(k,c)`（128-bit lane 逐列累加） |
| `matrix<R,C> * vector<float,C>` | 结果 `vector<float,R>`；维度不匹配时模板推导失败 |
| `transpose(m)` | `matrix<R,C>` → `matrix<C,R>`；4×4 走整 lane 转置，其余形状逐元素搬运 |
| `matrix<R,R>::identity()` | 单位矩阵；**只对方阵存在**，必须显式调用 |
| `determinant(m)` | 只对 `matrix<R,R>` 存在；局部副本 + 部分主元消元，`float` 中间量 |
| `try_inverse(m, out)` | 只对 `matrix<R,R>` 存在；checked，失败时 `out` **逐字节不变**，`&out == &m` 安全 |

1. **不提供**逐元素乘的 `operator*(matrix, matrix)`，也不按维度猜乘法：`matrix<2,3> * matrix<2,3>`
   编译失败。
2. `try_inverse` 的近奇异判据**与 scale 相关**：`|det(A)| <= kNearSingularRelativeThreshold * scale^Rows`，
   其中 `scale = max_r Σ_c |A(r,c)|`（∞-范数）。因此把矩阵整体乘 `s` 不会改变"是否被判为近奇异"。
   阈值是 `64 * ε ≈ 7.63e-6`，是**分类常量**，不是精度或条件数承诺：条件数远超 `1/阈值` 的矩阵
   即便被接受，`float` 求逆结果也没有有效位数，属于调用方责任。
3. `try_inverse` 以 **finite input 为契约前置**：非有限元素用位模式前检判出并返回 `false`
   （不依赖 `isfinite`，不会被 fast-math 消掉）；该前检只解决"非有限值分类"，不构成精度结论。
4. 列 padding 不参与任何矩阵运算：列载入时先按位清零，所以即使有人用字节手段把 `NaN` 灌进
   列 padding，矩阵乘 / 转置 / `try_inverse` 的结果也**逐位不变**。
5. `get_element` 是性能路径；`m[row][col]` 返回的 `RowProxy` / `ConstRowProxy` 只持有
   "矩阵指针 + 行号"，**无整行拷贝、无堆分配**、`const` / 非 `const` 正确，但**不承诺零开销**
   （D0-04）；右值矩阵的 `operator[]` 被 `= delete`（避免悬空代理），在临时矩阵上写
   `matrix<3,3>::identity()[0][0]` 是**编译错误**。

## 7. `pack` / `unpack`：跨 backend 转换与序列化

```cpp
using namespace ID::Math::SIMD;

vec3 v{1.0f, 2.0f, 3.0f};
float buffer3[3];
pack(v, buffer3);                 // 只写 N 个逻辑分量，padding lane 不写出

vec3 restored;
unpack(buffer3, restored);        // 逻辑分量相同，padding lane 为 +0.0f

matrix<3, 3> m = matrix<3, 3>::identity();
float buffer9[9];
pack(m, buffer9);                 // 紧凑列主序：out[c * Rows + r] == m(r, c)，不含列 padding
matrix<3, 3> restored_matrix;
unpack(buffer9, restored_matrix);
```

- 格式固定：**向量**是 `N` 个 `float`；**矩阵**是 `Rows * Cols` 个 `float` 的**紧凑列主序**。
  两者都与 `StorageN` / `ColumnStride` / `sizeof` 无关，可作为稳定的交换格式。
- 跨 backend 转换只能**显式逐元素**或走 `pack` / `unpack`：`ID.Math.Scalar` 与 `ID.Math.SIMD` 的
  同名类型互不相关，**禁止** `reinterpret_cast`，也**禁止**用 `memcpy` 直接搬对象内部布局。

## 8. 集成前提（组装侧与消费侧）

1. **组装登记状态**：`Assemble/catalog.cmake` 的 `ID_ASSEMBLY_CATALOG_MANIFESTS` 已包含
   `IDMathSIMD/plugin.json`，`Assemble/profiles/math-simd.json` 与 `math-both.json` 的
   `enabled_plugins` 也列出了 `"ID.Math.SIMD"`。登记属于组装层 owner 的职责（`Assemble/**`），
   本插件不改这些文件；**本 backend 自己没有执行产品构建取证**——产品内的 configure / 链接 /
   身份宏注入与运行结论只能由组装层与 Benchmark 侧的真实构建给出（见第 10 节"未验证"）。
2. **登记要求**（由组装层 owner 执行，属于 `Assemble/**`，本插件不改这些文件）：
   - `Assemble/catalog.cmake` 的 `ID_ASSEMBLY_CATALOG_MANIFESTS` 追加一行
     `${CMAKE_CURRENT_LIST_DIR}/../IDPlugin/IDMathSIMD/plugin.json`；
   - 需要用到它的 profile 的 `enabled_plugins` 显式加入 `"ID.Math.SIMD"`（组装层**不自动补依赖**）；
   - 同一产品内 `ID.Math.Scalar` 与 `ID.Math.SIMD` 是**两个不同的 capability**，不会触发
     `DuplicateCapability`；schema1 下若把两者放进同一个依赖组，会因"组内必须恰好命中 1 个"被判
     `AmbiguousProvider`——要允许并存必须迁到 schema2 的逐候选契约。
3. **消费侧只在编译期选择实现**：按实际拿到的身份宏决定包含哪个 backend 的伞头，典型形态是
   `#if defined(ID_USE_PLUGIN_IDMATHSCALAR)` / `#elif defined(ID_USE_PLUGIN_IDMATHSIMD)` / `#else #error`。
   `A only` / `B only` / `A+B` 三种产品都必须能编译；`A+B` 产品里两个宏都存在，消费者必须显式决定用途，
   不得依赖宏出现的顺序。**禁止**运行期分支或函数指针搜索来"找到当前后端"。
4. **只 include 伞头**：`#include "IDMathSIMDApi.hpp"`。不要直接包含子头或 `detail/**`（不承诺稳定性）。
   include 路径与身份宏都来自真实上游 target 的 PUBLIC usage requirement，不要手写
   `target_include_directories` / `target_compile_definitions` 复制一份。
5. 不要单独链接 `IDMathSIMD` target，通过产品消费目标（`IDGameEngine` / `IDAssembly` 链路）得到它。

## 9. 明确禁止的用法

- `union` punning、把 `storage` 当第二视图重新解释；重复存储 `x/y/z/w`；表达式模板。
- **跨 backend 的 `reinterpret_cast`**：`ID::Math::SIMD::vec3` 与 `ID::Math::Scalar::vec3` 是不同类型，
  互换必须显式逐元素或用 `pack` / `unpack`。
- 把内部布局当交换格式：外部序列化**不得**直接 `memcpy` 对象（padding 与 `ColumnStride` 不是稳定格式），
  必须显式逐元素或走 `pack` / `unpack`。
- 读取 padding lane / 列 padding 槽位，或用 `sizeof` 反推布局、假设两个 backend 同布局。
- 把 `m[row][col]` 的代理当 row-contiguous `span`，或把它当作"零开销"承诺。
- 用越界 `index` / `row` / `col` 访问：所有访问入口都不做边界检查。
- 把 intrinsic 值当第二份持久状态：对象里只有 `storage` 一份可写状态。
- 为满足 ISA 而在消费方或插件里手写 `target_compile_options` / `target_link_options` 重复规则。
- 依赖 fast-math 下的 `NaN` 比较语义（见第 5 节第 5 条）。

## 10. 验证状态（实测）

**正确性（double oracle 对照）**：393 项断言全部通过。相对误差只在 `|oracle| >= 1e-3` 的样本上统计
（避免"除以近零值"的无意义放大），MinGW GCC 16.2.0 x64、`-O2` 口径：

| 算子族 | 参与统计样本 | 最大相对误差 |
|---|---|---|
| `operator/`（向量除标量） | 9 | 0（逐分量与 double 取整一致） |
| `dot` | 3 | 0 |
| `squared_length` | 3 | 0 |
| `length` | 3 | 3.47e-8 |
| `normalize_unchecked` | 9 | 7.84e-8 |
| `matrix * matrix`（27 种组合） | 243 | 9.60e-7（`-O3 -ffast-math` 下 1.12e-6） |
| `matrix * vector`（9 种形状） | 26 | 2.51e-5（相消场景） |
| `determinant`（2/3/4 阶） | 24 | 4.16e-7 |
| `try_inverse`（2/3/4 阶） | 232 | 3.68e-7 |

**配置矩阵（每种配置都真实编译 + 运行）**：

| 配置 | 结果 |
|---|---|
| MinGW `-std=c++20 -O2 -Wall -Wextra -Wpedantic -fno-rtti -fno-exceptions` | 编译退出码 0（无警告）；运行退出码 0，393/393 |
| MinGW `-O3 -ffast-math -fomit-frame-pointer` | 编译 0；运行 0，393/393 |
| MinGW `-O3 -ffast-math -mavx2 -mfma`（工程 Release 的 ISA 传播口径） | 编译 0；运行 0，393/393 |
| MSVC 19.52.37025 x64 `/std:c++20 /W4 /GR- /EHs-c- /O2 /fp:precise` | 编译 0（无警告）；运行 0，393/393 |
| MSVC x64 `/O2 /fp:precise /arch:AVX2` | 编译 0；运行 0，393/393 |
| MSVC x64 `/O2 /fp:fast` | 编译 0；运行 0，391/393 + 2 项已知 fast-math 差异（见下） |

`/fp:fast` 的 2 项差异是 `equals(nan, nan)` 在 `vec2` / `vec3` 实例化上被编译器折成 `true`：
这是 `/fp:fast` 假定"无 NaN"的语义（第 5 节第 5 条已披露），不是运算实现缺陷——同一份源码在
`/fp:precise` 下 393/393 通过，`-ffast-math` 的 GCC 也没有做这个折叠。

**覆盖范围**：向量 2/3/4 分量全部运算（含 `cross` / `cross2` / `hadamard` / `equals` / `near_equal` /
`pack` / `unpack`）；`vec3` padding 在 `add` / `sub` / `hadamard` / 标量乘除 / `dot` /
`squared_length` / `length` / `normalize_unchecked` / `try_normalize` / `cross` / `equals` /
`near_equal` 之后均不污染结果，且结果 padding lane 为 `+0.0f`；27 种矩阵乘组合、9 种矩阵乘向量、
9 种转置（含矩形与 4×4）、2/3/4 阶行列式与求逆（良态、近奇异、精确奇异、含 NaN、整体缩放
×1e-4 / ×1e4）、`try_inverse` / `try_normalize` 失败时输出逐字节不变、右值 `operator[]` 拒绝。

**负向（按具体诊断断言，均退出码 1）**：

| 用例 | 诊断 |
|---|---|
| `g++ -mno-sse2` | `IDMathSIMDIsaGate.hpp:49: error: #error "ID.Math.SIMD 需要 SSE2…"` |
| `g++ -m32` | `IDMathSIMDIsaGate.hpp:34: error: #error "…基线 ISA 是 x64 SSE2：当前目标不是 x86_64…"` |
| `vector<int,3> + vector<int,3>` | `no match for 'operator+'` |
| `vector<double,3> + vector<double,3>` | `no match for 'operator+'` |
| `vector<float,8>` 的 `dot` | `no matching function for call to 'dot(…vector<float, 8>…)'` |
| `cross(vec2, vec2)` | `no matching function for call to 'cross(…)` |
| `matrix<2,3> * matrix<2,3>` | `no match for 'operator*'` |
| `determinant(matrix<2,3>)` | `no matching function for call to 'determinant(…)` |
| `matrix<5,5>` | `template constraint failure … is_valid_matrix_extent_v` |
| `matrix<2,2>::identity()[0][0]` | `use of deleted function … operator[](std::size_t) &&` |

**机器码抽查（只做指令层面观察，不做性能评级）**：

| 观察项 | 结论 |
|---|---|
| `dot` / `operator+`（2/3/4 分量）的实例化 | 无函数指针调用（`call *` / `call qword ptr` 计数为 0）；指令是 `movaps` / `movss` / `movsd` / `andps` / `mulps` / `addps` / `movhlps` / `shufps` / `addss` 一类的 SSE/SSE2 |
| `vec2` 的载入 | 只用 8 字节 load（`movss` 成对 / `movsd`），无 16 字节 load |
| `matrix * matrix` / `transpose`(4×4) | 列式 128-bit 指令（`mulps` / `addps` / `shufps` 广播 / `movups` 列写回），无函数指针调用 |
| `m[row][col]` 代理读取 / `get_element` | 单条按固定偏移的标量读取（`movss [reg+36]`），**无整行拷贝**、无调用 |
| 指令合法性 | 抽查到的全部是 SSE/SSE2 指令（`andps` / `mulps` / `movdqa` 等）；未出现对齐违规访问 |

**未验证**：`ID_MONOLITHIC_BUILD=ON` 静态归并、Clang、非 Windows 平台、MSVC 32 位目标
（ISA gate 的 MSVC 分支要求 x86_64，但没有 x86 工具集可取负向证据）、**产品内构建与运行**
（catalog 与 profile 登记已在组装层就位，但本 backend 没有执行产品 configure / 链接 / 运行取证，
身份宏注入与 `selected` / `slot` 证据只能由组装层构建给出）、性能与基准。本文不含任何性能数字或评级。
