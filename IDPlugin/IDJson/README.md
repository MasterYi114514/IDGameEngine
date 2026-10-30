# IDJson

JSON 读取与写出库：把 JSON 文本解析成紧凑的只读 DOM（`JsonValue`），并把 DOM 写回 JSON 文本。
解析产生的字符串、数组、对象都由 `JsonDocument` 内嵌的分段 arena 持有，一个文档一次清理。

- 命名空间：`ID::Json`
- 交付形态：`STATIC`（源码交付：公开头 + `src` + 构建清单 + 本文件）
- 依赖：`IDCommonHeaders`（`Common/Arena/SegmentedArena.hpp`、`Common/Core/Core.hpp`）；
  可选依赖 `IDLog`（仅用于解析失败的错误日志桥，见"配置项"）
- 构建入口：普通 CMake 目标 `IDJson`，不以先运行 `ID` 或读取清单为前提

本文件只写消费者可观察的行为与代价。性能数字一律以 `Benchmark` 的正式报告为准；**本插件当前性能未测量**，
本文件不给出任何估算或评级。

## 1. 能力概述

| 能力 | 说明 |
| --- | --- |
| 严格解析 | 按 RFC 8259 递归下降解析，拒绝尾逗号、注释、单引号、`NaN`/`Infinity` 字面量、根值后的多余内容；嵌套上限 256 层 |
| 两种所有权 | `parse` 深拷贝字符串（输入可立即释放）；`parse_non_owning` 对"无转义且长度 > 7"的字符串直接引用输入（零拷贝） |
| 零分配标量 | `null`/`bool`/`int64`/`double` 与 ≤ 7 字节的字符串完全内联在 16 字节的值里，不向 arena 申请 |
| 对象成员 | 插入序连续存放；成员数 ≤ 8 线性查找，> 8 建幂 2 开放寻址索引（FNV-1a，线性探测）；重复键 last-wins |
| 序列化 | 紧凑或缩进输出，直写调用方缓冲；`double` 用最短往返表示并补 `.0` 保持类型 |
| 错误定位 | 失败时给出错误码 + 字节偏移 + 行 + 列；失败后文档被置空 |
| 可选错误日志 | 挂接 `IDLog` 的 `Logger` 后，每次解析失败输出一条 `error`（含源名、错误码、行、列）；不挂接则零日志 |

不提供的能力（本期明确不做）：原地修改 DOM、单值释放、序列化到 arena、UTF-8 合法性校验、流式/增量解析、
注释或 JSON5 方言、`nullptr` 之外的空值表示。

## 2. 接口清单

### 2.1 公开头

| 头 | 内容 |
| --- | --- |
| `IDJsonCore.hpp` | 导出宏与编译期开关（`ID_JSON_STATIC`、`ID_JSON_ENABLE_LOG`） |
| `IDJsonValue.hpp` | `JsonType`、`JsonValue` 与内联访问器 |
| `IDJsonDocument.hpp` | `JsonDocument`、默认段来源 |
| `IDJsonParser.hpp` | `parse` / `parse_non_owning`、`JsonError`、`JsonParseError`、错误日志桥 |
| `IDJsonWriter.hpp` | `serialize`、`JsonWriteOptions`、`JsonWriteError` |
| `IDJson.hpp` | 伞头，一次包含以上全部 |

### 2.2 值（`IDJsonValue.hpp`）

```cpp
enum class JsonType : std::uint8_t
{
    Null = 0,
    Bool,
    Int,
    Double,
    InlineString,   // ≤ 7 字节字符串，字符内联在值里
    String,         // 长字符串，字符数据在 arena 或（non_owning 时）输入缓冲上
    Array,
    Object
};

inline constexpr std::uint32_t kInlineStringCapacity = 7;

struct JsonValue
{
    union alignas(8) Payload   // 强制 8 字节对齐：i386 SysV 的 alignof(double) 只有 4
    {
        void*         ptr;
        std::uint64_t u64;
        double        d;
        char          chars[8];
    };

    Payload  m_payload{};
    JsonType m_type{JsonType::Null};   // 读取请用 type()，不要直接比较这个成员
};

inline constexpr JsonValue kNullValue{};   // 越界/类型不匹配时返回的空值
```

`sizeof(JsonValue) == 16`（x86 与 x64 均由 `static_assert` 保证）。

只读访问器（全部 `noexcept`，不分配）：

| 访问器 | 语义 |
| --- | --- |
| `JsonType type() const` | 取类型标签 |
| `bool is_null() / is_bool() / is_int() / is_double() / is_array() / is_object() const` | 类型判断 |
| `bool is_string() const` | 字符串判断（`InlineString` 与 `String` 都返回 true） |
| `bool as_bool() const` | 非 `Bool` 返回 `false` |
| `std::int64_t as_int64() const` | 非 `Int` 返回 0；不做 `Int`/`Double` 隐式转换 |
| `double as_double() const` | 非 `Double` 返回 0.0；不做隐式转换 |
| `std::string_view as_string() const` | 非字符串返回空视图 |
| `std::size_t size() const` | 数组元素数 / 对象成员数；其它类型返回 0 |
| `const JsonValue& operator[](std::size_t index) const` | 数组下标；越界或非数组返回 `kNullValue` |
| `const JsonValue* find(std::string_view key) const` | 对象成员查找；未找到或非对象返回 `nullptr` |
| `const JsonValue* begin() const` / `end() const` | 数组遍历（插入序即下标序）；非数组两者都是 `nullptr` |
| `const ID::Json::Detail::ObjectEntry* members_begin() const` / `members_end() const` | 对象成员遍历（插入序）；非对象两者都是 `nullptr` |

对象的每个成员是 `Detail::ObjectEntry`，读成员用 `entry->key`（`const char*`）、`entry->key_length`、
`entry->value`。`Detail::` 下的类型是实现布局，随实现变更，不构成消费者契约。

### 2.3 文档（`IDJsonDocument.hpp`）

```cpp
inline constexpr std::size_t kDefaultBlockSize = 64 * 1024;

ID_JSON_API void* default_segment_alloc(std::size_t size, std::size_t alignment) noexcept;
ID_JSON_API void default_segment_free(void* block) noexcept;

class ID_JSON_API JsonDocument
{
public:
    explicit JsonDocument(std::size_t block_size = kDefaultBlockSize,
                          Common::SegmentAllocFn alloc_fn = default_segment_alloc,
                          Common::SegmentFreeFn free_fn = default_segment_free) noexcept;
    ~JsonDocument() noexcept;

    JsonDocument(const JsonDocument&)            = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    JsonDocument(JsonDocument&&)                 = delete;
    JsonDocument& operator=(JsonDocument&&)      = delete;

    bool is_ready() const noexcept;
    const JsonValue& root() const noexcept;
    void reset() noexcept;
    Common::SegmentedArena& arena() noexcept;
    const Common::SegmentedArena& arena() const noexcept;
};
```

- 不可拷贝、不可移动（值里的指针指向文档自己的 arena）。
- `reset()` 复用已申请的常规段、归还解析期申请的巨段，并把根值置空；**一次性作废文档内全部指针与
  `string_view`**，且不调用任何析构函数。
- `arena()` 只用于观察容量统计或解析器内部；从 arena 手工分配的内存不受文档管理。

### 2.4 解析（`IDJsonParser.hpp`）

```cpp
inline constexpr std::uint32_t kMaxJsonDepth = 256;

ID_JSON_API JsonError parse(JsonDocument& document, std::string_view text,
                            JsonParseError* out_error = nullptr) noexcept;
ID_JSON_API JsonError parse_non_owning(JsonDocument& document, std::string_view text,
                                       JsonParseError* out_error = nullptr) noexcept;
ID_JSON_API const char* to_string(JsonError error) noexcept;

ID_JSON_API void attach_error_logger(const ID::Log::Logger* logger, const char* source_name) noexcept;
ID_JSON_API void detach_error_logger() noexcept;
```

```cpp
struct JsonParseError
{
    JsonError       code{JsonError::none};   // 错误码
    std::uint32_t   offset{0};               // 出错位置的字节偏移
    std::uint32_t   line{1};                 // 行号，1 起
    std::uint32_t   column{1};               // 列号，1 起，按字节计
};
```

错误码 `JsonError`：

| 取值 | 含义 |
| --- | --- |
| `none` | 成功 |
| `out_of_memory` | arena 分配失败（段来源耗尽，或请求尺寸溢出） |
| `unexpected_token` | 语法意外的字符：未闭合的字符串/数组/对象、缺失冒号或逗号、尾逗号、非法字面量 |
| `invalid_number` | 数字字面量非法（含前导零、缺小数/指数位），或整数溢出后退化为 `double` 仍不可表示 |
| `invalid_string_escape` | `\` 转义非法，或 `\u` 后十六进制位不足/非法 |
| `invalid_utf16_surrogate` | `\uXXXX` 代理对不完整或孤立 |
| `control_char_in_string` | 字符串里出现未转义的 `< 0x20` 控制字符 |
| `depth_exceeded` | 嵌套层数达到 `kMaxJsonDepth` |
| `trailing_content` | 根值之后仍有非空白内容 |
| `input_too_large` | 单次输入超过 4 GiB - 1 字节：内部长度计量是 32 位，超过会回绕，因此整份输入直接拒绝，不进入解析 |
| `not_initialized` | 文档的 arena 未配置成功（`is_ready()` 为 false） |

`to_string(JsonError)` 返回稳定的静态字符串（如 `"unexpected_token"`），不需要释放。

### 2.5 序列化（`IDJsonWriter.hpp`）

```cpp
inline constexpr std::uint32_t kMaxJsonWriteDepth = 256;

struct JsonWriteOptions
{
    std::uint32_t indent = 0;   // 0 = 紧凑单行；> 0 = 每层缩进的空格数
};

enum class JsonWriteError : std::uint8_t
{
    none = 0,
    non_finite_number,   // 值里有 NaN 或 ±Inf：不是合法 JSON，未产生输出
    too_deep             // 嵌套超过 kMaxJsonWriteDepth
};

ID_JSON_API std::size_t serialize(const JsonValue& root, char* out, std::size_t capacity,
                                  const JsonWriteOptions& options,
                                  JsonWriteError* err = nullptr) noexcept;
ID_JSON_API const char* to_string(JsonWriteError error) noexcept;
```

- 返回值是**所需总字节数**（不含结尾 NUL）：`capacity` 不足时也返回该值，此时 `out` 的内容未定义。
- `out == nullptr` 时只探测所需字节数。
- `capacity >= 返回值 + 1` 时在 `out[返回值]` 写 `'\0'`；`capacity` 恰好等于返回值时不写。
- `double` 用最短往返表示；结果不含 `.`/`e` 时补 `.0`，保证读回来仍是 `Double` 而不是 `Int`。
- 字符串转义与解析对称：`"`、`\`、`\b`、`\f`、`\n`、`\r`、`\t` 用短转义，其它 `< 0x20` 用 `\u00XX`；
  `>= 0x20` 的字节原样透传（UTF-8 不校验）。

## 3. 最小可编译示例

```cpp
#include "IDJson.hpp"

#include <cstddef>

int main()
{
    ID::Json::JsonDocument document;

    const char* const text = "{\"id\":7,\"name\":\"demo\",\"tags\":[\"a\",\"b\"],\"ratio\":0.5}";

    ID::Json::JsonParseError error{};
    if (ID::Json::parse(document, text, &error) != ID::Json::JsonError::none)
    {
        // 解析失败：document.root() 已置空，error 里是错误码与行/列
        return 1;
    }

    const ID::Json::JsonValue& root = document.root();
    if (const ID::Json::JsonValue* id = root.find("id"))
    {
        if (id->is_int() && id->as_int64() != 7)
        {
            return 1;
        }
    }

    if (const ID::Json::JsonValue* tags = root.find("tags"))
    {
        for (const ID::Json::JsonValue* item = tags->begin(); item != tags->end(); ++item)
        {
            if (item->as_string().empty())
            {
                return 1;
            }
        }
    }

    char                    buffer[256];
    ID::Json::JsonWriteOptions options{};
    options.indent = 2;
    const std::size_t length = ID::Json::serialize(root, buffer, sizeof(buffer), options);
    if (length == 0 || length >= sizeof(buffer))
    {
        return 1;   // 0 表示序列化失败；>= 容量表示需要更大缓冲
    }

    document.reset();
    return 0;
}
```

构建（GCC / MinGW，假设插件根为 `IDPlugin/IDJson`、工程根为 `IDGameEngine`）：

```text
g++ -std=c++20 -O2 -I IDPlugin/IDJson/include -I IDPlugin/Common example.cpp \
    <构建目录>/IDPlugin/IDJson/libIDJson.a -o example
```

### 3.1 作为 CMake 目标消费

```cmake
# 插件自身的普通库构建入口已经在本目录的 CMakeLists.txt 里
add_subdirectory(IDPlugin)                       # 同时提供 IDCommonHeaders、IDLog、IDJson
target_link_libraries(YourTarget PRIVATE IDJson)
```

`IDJson` 目标自己声明了 `cxx_std_20`、`CXX_EXTENSIONS OFF`、公开 include 路径与 `ID_JSON_STATIC`，
消费者只链接即可。`IDLog` 是 `IDJson` 的私有依赖：消费者不需要包含 `IDLog` 头，但**最终可执行文件要能
找到 `IDLog` 的动态库**（`IDLog` 是 `SHARED` 形态，见"已知限制"）。

## 4. 配置项

| 配置 | 取值 | 默认 | 作用 |
| --- | --- | --- | --- |
| CMake 选项 `IDJSON_ENABLE_LOG` | `ON` / `OFF` | `ON` | 是否编译解析失败的错误日志桥；`OFF` 时不包含任何 `IDLog`/`spdlog` 头、不链接 `IDLog` |
| 宏 `ID_JSON_ENABLE_LOG` | `1` / `0` | `1` | 上面那个选项的实现层开关；构建清单只会为本库自己的翻译单元定义它，消费者不要定义 |
| 宏 `ID_JSON_STATIC` | 定义 / 不定义 | 由目标公开 | 静态库形态下把 `ID_JSON_API` 展开为空；由 `IDJson` 目标 `PUBLIC` 传递 |
| `JsonDocument` 构造参数 | `block_size`、`alloc_fn`、`free_fn` | `64 KiB`、malloc/free 包装 | 段大小与段来源；`free_fn` 为空时文档只丢弃段链、不归还 |
| `attach_error_logger(logger, source_name)` | `logger` / `source_name` | 未挂接 | 挂接错误日志器；`logger == nullptr` 等价于 detach |

`attach_error_logger` 的时序前提（按 `IDLog` 的契约）：

```text
ID::Log::init() → create_logger() → （可选 set_sink_path / set_sink_level） → attach_error_logger(logger, "source")
    → parse ... → detach_error_logger() → destroy_logger() → ID::Log::shutdown()
```

- `logger` 必须是 `IDLog` 以**非 const 对象**创建出来的指针（`create_logger`/`get_logger` 的返回值）；
  不得传入常量对象。
- `source_name` 只保存指针、不复制字符串：它指向的内容必须在 `detach` 之前保持有效（通常传字符串字面量）。
- `logger` 的有效期由调用方按 `IDLog` 契约保证：`destroy_logger`/`shutdown` 之后不得再让本库写日志，
  必须先 `detach_error_logger()`。

## 5. 线程与生命周期前提

- **一个文档同一时刻只归一个线程使用**。文档自身没有锁或原子操作，跨线程共享同一文档属于违约用法；
  不同线程各自持有各文档并行解析是安全的。
- `parse` / `parse_non_owning` 在调用线程内完成，不创建线程。
- `serialize` 是纯函数（只读值、只用调用方缓冲与栈上状态），可由多线程并发调用，读同一个 `root` 也安全。
- `attach_error_logger` / `detach_error_logger` 可在任意线程调用（内部是 relaxed 原子指针）；
  它们不提供比"指针可见性"更强的同步语义。
- 文档内全部指针与 `string_view` 在 `reset()` 或析构时**一次性失效**，此后使用属于悬垂访问。
- `parse` / `parse_non_owning` 进入时会先 `reset()` 文档：**上一次解析的结果在调用开始时就已失效**，
  不要跨 parse 调用保存指针。
- `parse_non_owning` 的输入缓冲必须存活到文档 `reset()`/析构；`parse` 的输入可以在返回后立即释放。

## 6. 失败语义

- 全部接口 `noexcept`，不使用异常与 RTTI；失败一律用返回值表达。
- 解析失败：返回具体 `JsonError`，`out_error` 里是错误码与位置，**文档被置空**（`root().is_null()`），
  不会残留半个对象。
- 文档未就绪（`block_size` 过小、`alloc_fn` 为空）：构造不失败，`is_ready()` 为 `false`，之后 `parse`
  返回 `not_initialized`。
- 单次输入超过 4 GiB - 1 字节：返回 `input_too_large`（`out_error` 的位置字段保持默认值，32 位偏移无法
  表达该范围），文档同样被置空，不进入解析。
- 序列化失败：返回 0 并给出 `JsonWriteError`；`non_finite_number` 时不产生任何输出。
- 容量不足不是失败：`serialize` 返回所需字节数，调用方扩容后重试。
- 类型不匹配的访问（`as_*`、`operator[]`、`find`、遍历）返回缺省值或空结果，不产生未定义行为。
- 越界下标返回 `kNullValue`（不越界读）。
- 解析失败日志只在**失败冷路径**产生一条 `error`；未挂接或选项 `IDJSON_ENABLE_LOG=OFF` 时为零日志。

## 7. 成本边界

| 路径 | 行为 |
| --- | --- |
| 标量读取（`as_*`、`type()`、`is_*`） | 只有内存读与比较：不分配、不追踪指针、不加锁 |
| 字符串读取 | 内联短串直接从值里取；长串一次指针解引用 |
| 数组/对象读取 | 值里存描述块指针，读元素/成员多一次解引用；对象成员 ≤ 8 线性扫描，> 8 哈希探测 |
| 解析热路径 | 空白判断是 256 项查表，字符串用 `memchr` 定界，数字用 `std::from_chars`；**不产生日志** |
| 解析分配 | 只发生在长字符串、数组/对象描述块与其扩容、对象索引；标量与短串零分配 |
| 解析扩容 | 数组/对象按 2 倍扩容并遗弃旧块（arena 不回收单块，留给 `reset()`/析构统一归还） |
| 解析错误路径 | 冷路径（`ID_NOINLINE` + `ID_COLD`）：行/列统计要从缓冲区起点数换行，是 O(已读长度) |
| 错误日志 | 每条失败一条 `error`；未挂接时只读一次原子指针即返回 |
| 序列化 | 单次遍历，不分配，不读 arena；缩进只用 `memset` 填空格 |

具体吞吐/延迟**未测量**：本文件不给数字、不给量级估计，也不做评级；正式读数以 `Benchmark` 的报告为准。

## 8. 已知限制

1. **UTF-8 不校验**：解析与序列化都按字节透传 `>= 0x20` 的内容，非法 UTF-8 不会被拒绝；需要合法性保证的
   调用方要自行校验（或保证输入可信）。
2. **对象键按字节比较**：区分大小写，不做 Unicode 规范化（`"é"` 的两种编码是不同键）。
3. **重复键 last-wins**：成员位置保持首次插入序，值取最后一次出现的值；重复键不会报错。
4. **不支持原地修改**：`JsonValue` 是只读视图；要改内容只能重新解析或重新解析写出的文本。
5. **`parse_non_owning` 的输入生命周期**：输入缓冲必须活到 `reset()`/析构，否则字符串视图悬垂；含转义的
   字符串在两种模式下都会复制到 arena（无法零拷贝）。
6. **深度上限固定**：`kMaxJsonDepth` 与 `kMaxJsonWriteDepth` 都是 256，不可由参数覆盖。
7. **`double` 超出可表示范围**：解析时报 `invalid_number`（不产生 `±Inf`）；序列化遇到 `NaN`/`±Inf` 报
   `non_finite_number`。
8. **`IDLog` 的动态库依赖**：`IDLog` 是 `SHARED`，选项 `IDJSON_ENABLE_LOG=ON` 时最终可执行文件要在运行期能找到
   `IDLog` 的动态库；把库复制到可执行文件旁边由消费方构建负责。
9. **错误码文本不含行列**：`to_string(JsonError)` 只给错误码名，位置由 `JsonParseError` 单独提供。
10. **32 位 GCC 只有编译期证据**：`sizeof(JsonValue) == 16` 由 `static_assert` 保证；`Payload` union 上的
    `alignas(8)` 让 `alignof(double) == 4` 的 32 位 ABI（i386 SysV）也停在 16 字节。本机 `g++ -m32`
    （MinGW 32 位 ABI，实测 `alignof(double) == 8`）已真实编译通过、断言在编译期生效；但本机没有 32 位
    运行库，`-m32` 无法链接与运行，`alignof(double) == 4` 的情形则是静态推导——两者都**未实测**。
11. **错误日志桥内部使用一次 `const_cast`**：`IDLog` 的日志方法是 `non const` 成员，而挂接接口按契约收
    `const Logger*`；因此 `logger` 必须是 `IDLog` 以非 const 对象创建的指针（见"配置项"）；传入指向真正
    const 对象的指针属于未定义行为。
12. **手工包含 vendor spdlog 头会在 x86 触发 4 条 fmt C4244**：若消费方用 `/I <IDLog>/vendor/spdlog/include`
    （或 `-I`）**手工**包含 vendor 头，x86 32 位的 fmt `float` 格式化模板会报 4 条 C4244（模板参数
    `unsigned __int64` → `size_t`）。`IDJson` 经 CMake 目标传递的 vendor 路径是 `SYSTEM`（`-isystem`），
    警告是否出现取决于消费方自己的构建方式；这不是 `IDJson` 源码产生的诊断。

## 9. 版本与接口版本

- 插件清单：`plugin_config.json`（当前字段只有 `name`、`dependencies`、`kind`、`requirements`）。
- **插件版本号与接口版本号字段仍未冻结**：清单 schema 冻结前本插件不写版本号，`README` 也不引用版本。
  接口的兼容性判断目前只能按公开头签名与"错误码/枚举取值顺序是否变化"来做。
- 破坏性变更（改签名、改枚举取值、改所有权或线程前提）必须同时更新公开头、本文件与测试交接。

## 10. 验证状态

- 已真实构建：MinGW GCC 16.2.0 / Release / `STATIC`（`-Wall -Wextra` 零警告）；
  MSVC 19.52 / Win32 x86 / Debug / `STATIC`（按工程默认警告级别 `/W3` 零警告；GCC 侧的警告集合是
  `-Wall -Wextra`）。构建目录与命令见交接记录。
- 开发自检：解析（owning / non_owning、嵌套、转义、代理对、数字边界、短串内联）、错误路径定位、
  序列化（紧凑/缩进/round-trip/非有限值）、对象索引与重复键、文档生命周期、错误日志桥（含 detach 后零新增），
  在 GCC 与 MSVC 两个组合上运行通过。
- 以上都是**开发自检，不是正式测试**；正式正确性测试与性能测量归 `Benchmark`，当前**性能未测量**。
- 未验证：32 位 GCC 的运行期（只有 `-m32 -c` 的编译期证据）、Linux、clang、`SHARED` 形态交付。
