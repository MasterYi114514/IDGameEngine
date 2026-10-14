# `ID.Math` 高性能 SIMD 数学插件工程架构与实现计划书

本文档是 `IDGameEngine` 数学插件 `ID.Math` 的实施计划，供审阅，也可直接交给后续开发或 AI Agent 按序落地。

## 1. 设计目标与核心定位

1. **架构分层定位**：属于 **Tier 1（高频内联插件）+ Tier 2（SoA 批量加速服务插件）混合架构**。
   - **单对象/局部算子（Tier 1 形态）**：`Vec2`、`Vec3`、`Vec4`、`Quat`、`Mat4` 的全部基础运算通过 `IDPlugin/include/Math/*.hpp` 纯头文件提供，强制使用 `ID_FORCEINLINE` + `ID_VECTORCALL` + `noexcept`，在调用方编译单元内 **100% 展开为原生 `XMM` / `YMM` 寄存器指令**，实现 **0 函数调用栈帧开销、0 跨 DLL 跳转开销**。
   - **批量数据流内核（Tier 2 插件形态）**：`ID.Math` 具备完整的微内核 `PluginDescriptor` 身份（`plugin_id = ID::hash_fnv_1a64("ID.Math")`），在 `on_register()` 阶段通过硬件 `cpuid` 检测当前 CPU 的指令集支持情况（SSE4.1 / AVX2+FMA），将面向 `ID.ECS` 与 `ID.Physics` 的 **SoA（Structure of Arrays）批量并行计算函数表**绑定到 `ID::ServiceSlot::Math` 槽位。
2. **严守引擎编码红线**：
   - 全局禁用异常与 RTTI（所有函数标记 `noexcept`）。
   - 全局静态数据（如单位矩阵、零向量、插件描述符、API 表）100% 采用 `constexpr` 或 `constinit`，杜绝静态初始化顺序灾难（SIOF）。
   - 遵循项目的 **紧凑 Allman 大括号 + 单行函数签名 + 列对齐成员变量 + `snake_case` 函数命名** 规范。

## 2. 全引擎统一数学空间与内存约定

为了避免未来 `ID.Physics`（物理）、`ID.Render`（渲染）和 `ID.Gameplay`（逻辑）在矩阵乘法顺序和坐标轴方向上产生混乱，`ID.Math` 确立以下不可动摇的数学公理。

### 2.1 三维坐标系：右手系（Right-Handed, Y-Up）+ NDC 深度范围 [0, 1]

- **世界与视图空间坐标轴**：
  - **+X 轴**：指向右方（Right）
  - **+Y 轴**：指向上方（Up）
  - **-Z 轴**：指向屏幕内/摄像机正前方（Forward = `(0, 0, -1)`），+Z 指向观察者后方。
  - **叉积定则**：遵循右手螺旋定则，即 `Right(+X) × Up(+Y) = Backward(+Z)`，因此 `Forward(-Z) × Up(+Y) = Right(+X)`。
- **裁剪空间与 NDC 深度范围**：
  - 采用现代图形 API（Vulkan / DirectX 12 / Metal）标准的 **Z_ndc ∈ [0, 1]**（近裁剪面 `z_near` 映射到 0，远裁剪面 `z_far` 映射到 1），而非传统 OpenGL 的 `[-1, 1]`。这能显著提升配合反向深度（Reverse-Z）时的浮点数深度缓冲精度。

### 2.2 矩阵内存布局与向量乘法约定

列主序存储（Column-Major）+ 列向量右乘（`v' = M · v`）。

`Mat4` 由 4 个 `Vec4` 列向量 `m_cols[0..3]` 组成，连续占 64 字节（1 个 CPU 缓存行）：

```text
M = [ c0 | c1 | c2 | c3 ]
  = | m00  m10  m20  tx |
    | m01  m11  m21  ty |
    | m02  m12  m22  tz |
    | 0    0    0    1  |
```

选择列主序 + 4 个 `Vec4` 列寄存器的原因：

1. **平移分量 `T = (tx, ty, tz, 1)` 恰好完整地落在第 4 列 `m_cols[3]` 的一个 `__m128` 寄存器中**。提取或修改物体的世界坐标只需 1 条寄存器读写指令，无需跨行拼凑。
2. 与 GLSL / SPIR-V（Vulkan 默认 `column_major`）的内存布局 **100% 二进制兼容**，向 GPU Constant Buffer / Push Constants 拷贝矩阵时可直接 `memcpy`，无需转置。
3. 当计算矩阵乘列向量 `v' = M · v = vx*c0 + vy*c1 + vz*c2 + vw*c3` 时，只需将 `v` 的 4 个分量分别广播（Shuffle）后与 `c0, c1, c2, c3` 做 **3 次 FMA（乘加指令 `_mm_fmadd_ps`）**，完全没有水平加法（Horizontal Add，在 SIMD 中极慢）。

## 3. 目录结构与文件职责

在 `IDPlugin/` 下构建以下文件结构：

```text
IDPlugin/
├── CMakeLists.txt                       # 注册 IDMath 插件目标，导出头文件目录
├── include/
│   └── Math/
│       ├── MathTypes.hpp                # [模块 1] SIMD 头文件引入、数学常量、标量快速运算与位操作
│       ├── Vec2.hpp                     # [模块 2] 8B 紧凑 2D 向量 (x, y)
│       ├── Vec3.hpp                     # [模块 3] 12B 紧凑存储型 3D 向量（专用于顶点缓冲与数组存储）
│       ├── Vec4.hpp                     # [模块 4] 16B 对齐 SIMD 向量 (__m128)，含 3D/4D 向量全部硬件算子
│       ├── Quat.hpp                     # [模块 5] 16B 对齐 SIMD 四元数 (x, y, z, w)，含旋转、Slerp 与矩阵互转
│       ├── Mat4.hpp                     # [模块 6] 64B (1 CacheLine) 对齐 4x4 矩阵，含 FMA 矩阵乘法、求逆、LookAtRH、PerspectiveRH_ZO
│       ├── BatchMath.hpp                # [模块 7] AVX2 (256-bit 8路并行) SoA 批量算子声明与内联实现
│       └── MathPlugin.hpp               # [模块 8] MathApiTable 服务表定义、CPU 特性结构与插件描述符声明
└── Math/
    └── src/
        └── MathPlugin.cpp               # [模块 8] CPUID 硬件指令集探测、ServiceSlot::Math 绑定与大批量算子实现
```

## 4. 各模块核心算法与底层优化

### 模块 1：`MathTypes.hpp`（常数与标量快速算法）

**编译期常数（`inline constexpr float`）**：

- `kPi = 3.14159265358979323846f`、`kTwoPi`、`kHalfPi`、`kInvPi`
- `kDegToRad = kPi / 180.0f`、`kRadToDeg = 180.0f / kPi`
- `kEpsilon = 1.0e-6f`、`kLargeEpsilon = 1.0e-4f`

**无分支标量工具（全部 `ID_FORCEINLINE constexpr`）**：

- `min`、`max`、`clamp`、`lerp`、`abs`、`sign`
- **硬件级单精度快速反平方根 `inv_sqrt_fast(float x)`**：使用 `_mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)))` 配合 1 次牛顿-拉弗森迭代（Newton-Raphson Iteration：`y_{n+1} = y_n * (1.5 - 0.5 * x * y_n^2)`），将相对误差控制在 `1e-6` 以内，耗时约 3 个周期。

### 模块 2 与 3：`Vec2.hpp` 与 `Vec3.hpp`（紧凑存储层）

如果将 `Vec3` 设为 `alignas(16)`，则每个 `Vec3` 末尾会有 4 字节 Padding。在存储包含 100 万个顶点的网格或 ECS 组件数组时，会白白浪费 25% 的内存容量和 L1/L2 缓存带宽。

设计约束：

- `Vec2`（`sizeof == 8`）与 `Vec3`（`sizeof == 12`）保持纯标量紧凑布局（`float x, y, z;`），加上 `static_assert(sizeof(Vec3) == 12)` 保护。
- 提供常规 `constexpr` 标量算子，用于编译期常量计算和简单赋值。
- **安全跨界加载到 SIMD（防越界 Page Fault）**：当需要把 `Vec3` 载入 `Vec4`（`__m128`）进行密集计算时，**绝不能直接对 `Vec3*` 强转调 `_mm_loadu_ps`**。如果该 `Vec3` 恰好位于 64KB 虚拟内存页的最后 12 个字节，多读 4 个字节会触发硬件访问违例。正确做法是使用 `_mm_loadl_pi` 读取前 8 字节 `(x, y)`，再用 `_mm_load_ss` 读取第 9~12 字节 `(z)` 并通过 `_mm_movelh_ps` 合并，保证 **只精确读取 12 字节且仅需 2 条指令**。

### 模块 4：`Vec4.hpp`（16B 对齐 XMM 寄存器核心层）

这是日常 3D/4D 几何计算的主力类型（3D 点用 `w = 1`，3D 方向向量用 `w = 0`，颜色/齐次坐标用 `(x, y, z, w)`）。

数据结构：

```cpp
struct alignas(16) Vec4
{
    union
    {
        __m128  m_simd;
        struct { float x, y, z, w; };
        float   m_data[4];
    };
};
```

**传参铁律**：所有接收 `Vec4` 的函数一律**按值传参**并标注 `ID_VECTORCALL`（如 `ID_FORCEINLINE Vec4 ID_VECTORCALL vec4_add(Vec4 a, Vec4 b) noexcept`），确保参数直接在 `XMM0 ~ XMM5` 寄存器中流转，绝不落栈。

关键 SIMD 算法：

1. **四则运算与 FMA 乘加**
   - `+`、`-`、`*`、`/`：单条 `_mm_add_ps`、`_mm_sub_ps`、`_mm_mul_ps`、`_mm_div_ps`。
   - `vec4_madd(Vec4 a, Vec4 b, Vec4 c)`：当开启 AVX2/FMA 时编译为单条 `_mm_fmadd_ps(a.m_simd, b.m_simd, c.m_simd)`（计算 `a * b + c`）。
2. **点积（Dot Product）**
   - 3D 点积 `vec3_dot(Vec4 a, Vec4 b)`：使用 SSE4.1 硬件指令 `_mm_dp_ps(a.m_simd, b.m_simd, 0x7F)`（掩码 `0111_1111`：只计算前三个分量之和并广播到 4 个通道）。
   - 4D 点积 `vec4_dot(Vec4 a, Vec4 b)`：使用 `_mm_dp_ps(a.m_simd, b.m_simd, 0xFF)`。
3. **3D 叉积（Cross Product，3 指令法）**

   计算 `a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx, 0)`：

   ```cpp
   __m128 a_yzx = _mm_shuffle_ps(a.m_simd, a.m_simd, _MM_SHUFFLE(3, 0, 2, 1));
   __m128 b_yzx = _mm_shuffle_ps(b.m_simd, b.m_simd, _MM_SHUFFLE(3, 0, 2, 1));
   __m128 c = _mm_fmsub_ps(a.m_simd, b_yzx, _mm_mul_ps(a_yzx, b.m_simd));
   return Vec4(_mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 2, 1)));
   ```

   在 `(z, x, y)` 顺序上做单次 FMA 乘减，最后再 shuffle 一次归位，比传统写两次两路 shuffle 省去 2 条 shuffle 指令。

4. **精确归一化与极速归一化**
   - 精确版 `vec4_normalize`：`_mm_div_ps(v, _mm_sqrt_ps(_mm_dp_ps(v, v, 0xFF)))`。
   - 极速版 `vec4_normalize_fast`：用 `_mm_rsqrt_ps` 硬件指令配合 1 次 FMA 牛顿迭代，供粒子系统、布料模拟等大吞吐量场景使用。

### 模块 5：`Quat.hpp`（16B 对齐 SIMD 四元数）

存储顺序为 `(x, y, z, w)`，其中 `(x, y, z)` 为虚部向量 `u`，`w` 为实部标量。单位四元数 `Quat::identity()` 为 `(0, 0, 0, 1)`。

核心算法：

1. **四元数乘法 `quat_mul(Quat a, Quat b)`**：使用 SSE 掩码异或与 FMA 指令组合，无分支完成汉密尔顿乘积。
2. **四元数旋转三维向量 `quat_rotate_vec3(Quat q, Vec4 v)`（Rodrigue 极速双叉积公式）**：
   - 传统公式 `v' = q · v · q^{-1}` 需要两次完整的四元数乘法（32 次标量乘法）。
   - 极速公式：设 `u = (qx, qy, qz, 0)`，计算 `t = 2 (u × v)`，`v' = v + qw * t + (u × t)`。只需调用 2 次上面的 3 指令 `vec3_cross` + 2 次 `_mm_fmadd_ps`，指令数直接砍半。
3. **球面线性插值 `quat_slerp(Quat a, Quat b, float t)`**：处理最短路径翻转（当 `dot(a, b) < 0` 时反转符号）以及夹角极小时自动退化为归一化线性插值（`nlerp`）防除零。

### 模块 6：`Mat4.hpp`（64B 缓存行对齐 4×4 矩阵）

数据结构：

```cpp
struct alignas(64) Mat4
{
    Vec4 m_cols[4]; // 列主序：m_cols[0..2] 为旋转缩放基向量，m_cols[3] 为平移列 (tx, ty, tz, 1)
};
static_assert(sizeof(Mat4) == kCacheLineSize);
```

`alignas(64)`（即 `kCacheLineSize`）使每一个 `Mat4` 恰好独占一条完整的 64 字节 CPU L1 缓存行，绝不跨越两条缓存行边界，避免 Cache Line Split 惩罚。

核心算法：

1. **矩阵乘向量 `mat4_mul_vec4(const Mat4& m, Vec4 v)`**：4 条 `_mm_shuffle_ps` 广播 + 1 条 `_mm_mul_ps` + 3 条 `_mm_fmadd_ps`，总计 **8 条指令**完成。
2. **矩阵乘矩阵 `mat4_mul(const Mat4& a, const Mat4& b)`**：对矩阵 `b` 的每一列 `b.m_cols[i]` 执行一次 `mat4_mul_vec4(a, b.m_cols[i])`。
3. **SIMD 分块求逆矩阵**：
   - `mat4_inverse_transform(const Mat4& m)`：针对刚体/仿射变换矩阵的极速求逆。当矩阵仅包含旋转、平移和缩放时，利用左上 `3×3` 块转置/正交性质直接求逆，耗时仅为通用求逆的约 1/4。
   - `mat4_inverse(const Mat4& m)`：通用 `4×4` 伴随 / 2×2 分块克拉默法则求逆，适用于含透视投影的任意可逆矩阵。
4. **右手坐标系 + [0, 1] 深度专用相机矩阵**：
   - `mat4_look_at_rh(Vec4 eye, Vec4 target, Vec4 up)`：计算相机前向轴 `f = normalize(target - eye)`，右轴 `s = normalize(f × up)`，真上轴 `u = s × f`。由于右手系相机看向 `-Z`，第三行存入 `-f`。

   ```text
   V_RH = | sx   sy   sz   -(s·eye) |
          | ux   uy   uz   -(u·eye) |
          |-fx  -fy  -fz    (f·eye) |
          | 0     0    0     1       |
   ```

   - `mat4_perspective_rh_zo(float fov_y_rad, float aspect, float z_near, float z_far)`：严格推导右手系（View Space `z < 0`）映射到 `Z_ndc ∈ [0, 1]` 的透视矩阵。其中 `h = 1 / tan(fov_y / 2)`，`w = h / aspect`：

   ```text
   P_RH_ZO = | w  0  0                         0                                    |
             | 0  h  0                         0                                    |
             | 0  0  z_far/(z_near-z_far)      (z_near*z_far)/(z_near-z_far)        |
             | 0  0 -1                         0                                    |
   ```

   验算：当点在近平面 `z = -z_near` 时，`z_clip = 0`，因此 `z_ndc = 0`；当点在远平面 `z = -z_far` 时，`w_clip = z_far`，`z_clip = z_far`，因此 `z_ndc = z_clip / w_clip = 1`。

### 模块 7 与 8：`BatchMath.hpp` 与 `MathPlugin.hpp/.cpp`

为了践行 **“不在循环内逐对象跨插件调用，而是以连续内存块做 SIMD 批处理”** 的设计，在 `ID.Math` 插件实体中实现 AVX2（256-bit `YMM` 寄存器，单指令同时处理 **8 个 `float`**）的批量数学内核，并通过 `ServiceSlot::Math` 暴露给全引擎。

`MathApiTable` 结构定义（位于 `MathPlugin.hpp`）：

```cpp
struct MathCpuFeatures
{
    bool sse41_supported = false;
    bool avx2_supported  = false;
    bool fma3_supported  = false;
};

struct MathApiTable
{
    MathCpuFeatures features;

    // SoA 批量线性运动积分：pos[i] += vel[i] * dt
    // 使用 AVX2 _mm256_fmadd_ps，每次循环同时更新 8 个刚体坐标
    void(*batch_integrate_soa)(
        float* ID_RESTRICT pos_x, float* ID_RESTRICT pos_y, float* ID_RESTRICT pos_z,
        const float* ID_RESTRICT vel_x, const float* ID_RESTRICT vel_y, const float* ID_RESTRICT vel_z,
        size_t count, float dt) noexcept;

    // 批量矩阵变换 4D 点云：out_pts[i] = transform * in_pts[i]
    void(*batch_transform_vec4)(
        Vec4* ID_RESTRICT out_pts, const Vec4* ID_RESTRICT in_pts,
        size_t count, const Mat4& transform) noexcept;

    // 批量矩阵级联乘法（骨骼动画层级矩阵合成）：out[i] = parent[i] * local[i]
    void(*batch_mul_mat4)(
        Mat4* ID_RESTRICT out_mats, const Mat4* ID_RESTRICT parent_mats,
        const Mat4* ID_RESTRICT local_mats, size_t count) noexcept;
};
```

双模零开销分派：

- 在 `IDPlugin/CMakeLists.txt` 中通过 `id_add_plugin(IDMath SOURCES Math/src/MathPlugin.cpp)` 注册该插件。
- 当开启 `ID_MONOLITHIC_BUILD=ON`（Release 态）时，`IDMath` 编译为开启 LTO 的静态库；在 Dev 态下编译为 `libIDMath.dll`，导出 `kMathPluginDesc` 供宿主注册。

## 5. 实施步骤检查表

按以下 5 步推进：

- **Step 1**：创建 `IDPlugin/include/Math/MathTypes.hpp`、`Vec2.hpp`、`Vec3.hpp`，完成基础常量、安全 12B 加载与标量工具。
- **Step 2**：创建 `IDPlugin/include/Math/Vec4.hpp` 与 `Quat.hpp`，实现 SSE4.1/FMA 16B 对齐寄存器算子（点积、3 指令叉积、Rodrigue 四元数旋转、Slerp）。
- **Step 3**：创建 `IDPlugin/include/Math/Mat4.hpp`，实现 64B 缓存行对齐列主序矩阵、FMA 乘法、快速仿射求逆，以及右手系 `[0, 1]` 深度的 `mat4_look_at_rh` 与 `mat4_perspective_rh_zo`。
- **Step 4**：创建 `IDPlugin/include/Math/BatchMath.hpp`、`MathPlugin.hpp` 与 `IDPlugin/Math/src/MathPlugin.cpp`，实现 CPUID 指令集检测、AVX2 8 路并行 SoA 批处理内核及 `ServiceSlot::Math` 服务表绑定，并在 `IDPlugin/CMakeLists.txt` 中注册 `IDMath` 目标。
- **Step 5**：在 `Sandbox/src/main.cpp` 中接入真实的 `IDMath` 插件，验证：
  1. 右手系透视投影矩阵将近/远平面点精确映射到 `Z_ndc = 0` 和 `1`；
  2. 四元数旋转与旋转矩阵结果误差 `< 1e-5`；
  3. 跑基准测试对比 **SIMD `Mat4` / AVX2 `BatchMath`** 与 **普通 C++ 标量循环** 的实测加速比（预期提速 **6x ~ 12x**）。
