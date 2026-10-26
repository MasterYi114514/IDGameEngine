# IDLog

基于内嵌 spdlog（`vendor/spdlog` 1.17.0，header-only）的日志插件。对外只暴露本插件的契约：
日志器句柄、`Logger` 对象、级别枚举与目标类型枚举；底层日志库类型不出现在公开 API 上。

- 链接形态：`SHARED`，公开头 + 导入库；产物名 Windows/MSVC 为 `IDLog.dll`，MinGW 为 `libIDLog.dll`。
- 交付方式：源码（公开头 + `src`）；三方库随插件自带，保留 `LICENSE` 与 fmt 许可。
- 就绪条件：先 `ID::Log::init()`，否则 `create_logger` 返回无效句柄（不隐式初始化）。

## 1. 能力

- fmt 风格格式化：`logger->info("value = {}", x)`，格式串在编译期检查。
- 每个日志器可独立启停两个输出目标：终端（彩色）、文件；目标由 `SinkType` 在运行期选择。
- 两个输出目标各自有独立的级别裁剪阈值（`ID::Log::Level`）。
- `fatal` 记 critical、flush 后终止进程，且不受任何级别配置影响。
- `shutdown` 自动 flush 并清理全部残留日志器，之后可再次 `init`。

本期**不提供**：异步写、Network 目标、日志级别以上（高于 critical）的阈值、轮转文件、进程级控制台设置修改。

## 2. 接口清单

### 2.1 生命周期与登记（`IDLog.hpp`，DLL 导出）

```cpp
namespace ID::Log
{
    using LoggerHandle = ::ID::Common::Handle<std::uint32_t, Logger>;

    ID_LOG_API void         init();
    ID_LOG_API void         shutdown();
    ID_LOG_API LoggerHandle create_logger(const char* name);
    ID_LOG_API Logger*      get_logger(const LoggerHandle handle);
    ID_LOG_API void         destroy_logger(const LoggerHandle handle);
}
```

| 接口 | 语义 |
| --- | --- |
| `init()` | 幂等；无失败返回。未初始化时 `create_logger` 返回无效句柄。与 `shutdown` 不得并发。 |
| `shutdown()` | flush 全部存活日志器 → 销毁残留日志器（释放底层 logger 与 sink）→ 复位登记状态。未初始化时是无操作；之后可再次 `init()`。使所有句柄与 `Logger*` 失效，此后经旧 `Logger*` 的任何调用属未定义行为（见第 5、6 节）。句柄不跨生命周期：重新 `init` 后旧句柄数值可能被新日志器复用（ABA）。 |
| `create_logger(name)` | `name` 为 `%n` 显示的日志器名（UTF-8），可为 `nullptr`（按空名）。成功返回有效句柄；未初始化／内存不足／后端创建失败返回无效句柄。默认启用终端目标（阈值 `Trace`），文件目标停用。若构造期间另一线程完成了 `shutdown()`，返回无效句柄且不登记（见第 5 节）。 |
| `get_logger(handle)` | 返回 `Logger*`；句柄无效、已销毁或已 `shutdown` 时返回 `nullptr`。`shutdown` 后重新 `init` 的旧句柄可能命中新日志器（ABA），不能据返回值判断句柄"是否还属于本轮"。 |
| `destroy_logger(handle)` | flush 后释放该日志器；无效句柄或已销毁句柄是无操作（不会重复回收槽位）。 |

### 2.2 输出目标类型（`IDLogSink.hpp`）

```cpp
namespace ID::Log
{
    enum class SinkType : uint8_t
    {
        Console = 0,    // 终端（彩色）输出；创建日志器时默认启用
        File    = 1     // 文件输出；路径由 `Logger::set_sink_path` 提供，默认停用
    };
}
```

本期只有这两个取值：其它目标（例如网络）在真正实现时才增加枚举值，不预先占位。

### 2.3 日志器（`IDLogger.hpp`）

```cpp
namespace ID::Log
{
    class Logger
    {
    public:
        template <typename... Args> void trace(fmt::format_string<Args...> format, Args&&... args);
        template <typename... Args> void debug(fmt::format_string<Args...> format, Args&&... args);
        template <typename... Args> void info(fmt::format_string<Args...> format, Args&&... args);
        template <typename... Args> void warn(fmt::format_string<Args...> format, Args&&... args);
        template <typename... Args> void error(fmt::format_string<Args...> format, Args&&... args);
        template <typename... Args> void fatal(fmt::format_string<Args...> format, Args&&... args);

        ID_LOG_API bool enable_sink(SinkType type);
        ID_LOG_API bool disable_sink(SinkType type);
        ID_LOG_API bool set_sink_level(SinkType type, Level level);
        ID_LOG_API bool set_sink_path(SinkType type, const char* path);
    };
}
```

级别映射（`Level` 的底层类型与取值不变）：

| `ID::Log::Level` | 底层级别 | 说明 |
| --- | --- | --- |
| `Trace` | trace | |
| `Debug` | debug | |
| `Info` | info | |
| `Warn` | warn | |
| `Error` | err | 触发一次 flush（阈值 `err`） |
| `Fatal` | critical | 记日志后 flush 并 `std::abort()`；不受任何级别配置影响 |

配置方法（四个方法都返回 `bool`：失败只以 `false` 表达，异常不跨越 DLL 边界）：

| 方法 | 语义 |
| --- | --- |
| `enable_sink(type)` | 启用目标。`Console` 在 `create_logger` 时已启用，重复调用幂等并返回 `true`。`File` 只有在 `set_sink_path` 成功过之后才可能启用；未设置路径或目标创建失败时返回 `false`，且现有配置不变。日志器已销毁返回 `false`。 |
| `disable_sink(type)` | 先 flush 再摘除目标；目标对象与阈值保留，文件句柄保留到日志器销毁或切换到其它路径，再次 `enable_sink` 复用同一对象。日志器仍存活且 `type` 合法时返回 `true`（目标本来未挂载也算成功）；非法 `type` 返回 `false` 且不改动任何已挂载目标。两个目标都停用时后端阈值置为 off：没有输出，且消息在格式化之前就被挡掉。 |
| `set_sink_level(type, level)` | 设置该目标的阈值（合法 `level` 为 `Level::Trace` 到 `Level::Fatal`）；对尚未创建或已停用的目标同样记录，启用时生效。日志器仍存活且 `type`、`level` 都合法时返回 `true`；非法 `type` 或非法 `level`（强制转换后超出 `Fatal`）返回 `false` 且不改动任何阈值。 |
| `set_sink_path(type, path)` | 设置路径并启用该目标。只有 `SinkType::File` 合法，`Console` 返回 `false`。路径为空、文件打不开、内存不足或日志器已销毁返回 `false`，且已有配置保持可用（新目标挂载成功后才摘除旧目标）。同一路径重复调用幂等（该目标被停用时重新接入）；不同路径先创建并挂载新目标，成功后才 flush 并关闭旧目标。追加写入，不截断已有内容。 |

`type` 是运行期参数：把非法的 `SinkType` 值（超出枚举取值的整数）强制转换后传入，四个方法都返回 `false`，
不会静默当作 `Console` 或 `File` 处理，也不改动任何已挂载目标与阈值。

`level` 同样是运行期参数：把超出 `Level::Fatal` 的值强制转换后传给 `set_sink_level`，该方法返回 `false`
且**不改动任何阈值**（与非法 `SinkType` 同策略，见第 6 节），不会被"就近归一"到某个合法阈值。
该校验只作用于这一个配置入口：`trace` / `debug` / `info` / `warn` / `error` / `fatal` 六个方法的级别
由插件内部常量给出，不经过运行期校验。

返回值是唯一的失败观测渠道——忽略返回值即放弃对"该目标是否真正可用"的观测，此时"消息没出现在该目标"
与"目标未挂载／配置被拒绝"不可区分。

模式串：`[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v`（终端带色彩标记，文件 sink 忽略色彩标记）。
消息与模式串按 UTF-8 解释。

## 3. 最小可编译示例

```cpp
#include "IDLog.hpp"

int main()
{
    ID::Log::init();

    const ID::Log::LoggerHandle handle = ID::Log::create_logger("app");
    if (!handle.is_valid())
    {
        return 1;
    }

    ID::Log::Logger* logger = ID::Log::get_logger(handle);
    if (logger == nullptr)
    {
        return 1;
    }

    logger->info("this is an example {}, {}", 1, "two");

    logger->set_sink_level(ID::Log::SinkType::Console, ID::Log::Level::Info);
    logger->set_sink_level(ID::Log::SinkType::File, ID::Log::Level::Debug);
    if (!logger->set_sink_path(ID::Log::SinkType::File, "app.log"))
    {
        logger->warn("file target unavailable");
    }

    logger->error("{} logged", "error");

    ID::Log::destroy_logger(handle);
    ID::Log::shutdown();
    return 0;
}
```

构建接线（普通 CMake 库消费，不经过项目组装）：

```cmake
add_executable(app main.cpp)
target_link_libraries(app PRIVATE IDLog)     # 公开 include 与 vendor 头由 IDLog 目标传递
```

## 4. 配置项与默认值

| 项 | 默认值 | 可否原地修改 | 备注 |
| --- | --- | --- | --- |
| 终端目标 | 启用 | 是（`enable_sink` / `disable_sink`，`SinkType::Console`） | 阈值默认 `Trace` |
| 文件目标 | 停用 | 是（`set_sink_path` / `enable_sink` / `disable_sink`，`SinkType::File`） | 需先成功设置路径；追加写入 |
| 终端阈值 | `Trace` | 是（`set_sink_level`，`SinkType::Console`） | 每目标独立 |
| 文件阈值 | `Trace` | 是（`set_sink_level`，`SinkType::File`） | 仅在文件目标启用后产生效果 |
| flush 时机 | 被放行的 `Error` 及以上级别消息、`fatal`、`disable_sink`、`set_sink_path` 切换路径时、`destroy_logger`、`shutdown` | 无配置入口 | 受 logger 阈值联动：若所有已启用目标的阈值都高于 `Error`，该类消息在格式化前就被丢弃，不会触发该 flush。不保证掉电级持久（不做 fsync） |
| Network 目标 | 不存在 | — | 未提供：`SinkType` 无该取值、无配置入口；不参与阈值计算 |

插件不读取环境变量与配置文件；配置由消费者通过上述方法提供。

## 5. 线程与生命周期前提

- `trace` / `debug` / `info` / `warn` / `error` / `fatal` 可由多线程并发调用，包括同一 `Logger` 对象。
- 配置方法（`enable_sink` / `disable_sink` / `set_sink_level` / `set_sink_path`）**非线程安全**：不得与任何线程的日志调用并发执行。
- 五个登记接口之间由插件互斥保护；`init` 与 `shutdown` 本身必须由产品侧串行调用。
  `create_logger` 的后端构造发生在锁外，插入前会在锁内重新确认仍处于已初始化状态：若构造期间另一线程
  完成了 `shutdown()`，该次调用返回无效句柄，不会把日志器登记进一个已关闭的登记表。这是"登记接口与
  `shutdown` 互斥"的具体含义，不构成让 `init` / `shutdown` 并发调用合法的许可。
- `get_logger` 返回的 `Logger*` 只保证到下一次 `create_logger` / `destroy_logger` / `shutdown` 之前有效。
  需要长期持有时保存 `LoggerHandle`，每次使用前重新 `get_logger`。
- `destroy_logger` 之后句柄失效；该槽位可能被后续 `create_logger` 复用，调用方不得再使用已销毁的句柄。
- `LoggerHandle` 与 `Logger*` 都只属于**当前那一轮** `init` / `shutdown` 生命周期。`shutdown()` 之后
  重新 `init()`，槽位下标从 0 重新分配，旧句柄的数值可能被新日志器复用（ABA）：`get_logger(旧句柄)`
  此时返回的是**新日志器**而不是 `nullptr`，`nullptr` 只在同一轮生命周期内才是"句柄不可用"的判据。
  调用方必须重新 `create_logger`，不得跨轮保存或复用旧句柄；插件不提供代数/版本位，也不检测复用。
- 生命周期由产品决定：`shutdown` 必须显式调用（不依赖静态析构释放文件与 sink）。

## 6. 失败行为

- 可恢复失败一律以返回值表达：`create_logger` 返回无效句柄，`get_logger` 返回 `nullptr`，
  `enable_sink` / `disable_sink` / `set_sink_level` / `set_sink_path` 返回 `false`。异常不跨越 DLL 边界。
- 文件打不开不会终止进程；只让 `set_sink_path` 返回 `false`，已有配置不变（旧目标仍在工作）。
- `enable_sink(SinkType::File)` 在从未成功设置路径时返回 `false`，属于"未配置"而不是"配置失败"：
  它不创建目标、不改变任何已启用目标。
- 参数越界：四个配置方法收到非法的 `SinkType`，或 `set_sink_level` 收到非法的 `Level`（强制转换后
  超出 `Level::Fatal`），一律返回 `false` 且**不改动任何已挂载目标与阈值**；该路径不创建对象、不做 IO、
  也不写 stderr，返回值是唯一的观测渠道。不静默降级：非法 `Level` 不会被当成 `Trace` 或任何其它阈值。
- 运行期写失败／格式化失败与上面的"一次性配置失败"不同：已启用目标在运行期写入失败（磁盘满、
  句柄失效、运行期格式串不匹配等）时**不抛异常、不终止进程**，该条消息被**丢弃**，写入口仍然
  返回 `void`，**调用方没有逐条失败返回值可观测**；失败由底层日志库的错误处理器向 stderr 报告，
  且限频为约每秒最多 1 条（带累计计数）。该丢弃与限频来自底层实现行为，本插件未单独测量。
- `destroy_logger` 之后：该槽位对象**仍留在池内并被清空**，因此残留在旧 `Logger*` 上的日志调用不产生
  输出、四个配置方法返回 `false`（`Benchmark` 用例 `sink.destroyed_config_methods_false` 覆盖）；
  但这依赖实现细节，且槽位可能被后续 `create_logger` 复用、同一指针届时指向新日志器——所以"销毁后继续
  使用该指针"仍是违约用法，不得依赖上述行为。
- `shutdown` 之后：所有 `LoggerHandle` 与 `Logger*` **整体失效**（槽位对象的生命周期已结束），此后经旧
  `Logger*` 调用——**包括日志方法与四个配置方法**——属**未定义行为**，本插件**不承诺安全，也不承诺不崩溃**；
  要恢复使用必须重新 `init()` 并重新 `create_logger`。这条与上一条不同：`destroy_logger` 是定点的槽位
  清理，`shutdown` 是整体失效。
- 失效句柄的"复活"不是失败可观测项：`shutdown` 之后重新 `init`，旧 `LoggerHandle` 的数值可能被新日志器
  复用（ABA，见第 5 节），`get_logger` 返回新日志器而非 `nullptr`。插件不检测、不报告这种复用——跨轮
  复用旧句柄属调用方违约，必须重新 `create_logger`。
- 与 `fatal` 对照：`fatal` 的强语义是**终止进程**（`std::abort()`），而不是"日志一定落盘"——
  它的 flush 失败同样被吞掉，随后仍然终止，`fatal` 不返回；其余级别的失败只丢该条消息并继续运行。

## 7. 成本边界

- 日志写入是**冷路径**：不要放进逐帧、逐元素或逐分配路径。
- 被级别裁剪的消息：一次阈值读取 + 一次比较，不做格式化、不分配、不阻塞。
  阈值取“已启用目标阈值的最小值”，因此所有目标都会被丢弃的消息不会进入格式化。
- 未被裁剪的消息：**每个已启用目标各自格式化一次**（底层日志库按 sink 分别格式化，可能分配缓冲）
  并同步写出（可能阻塞在 IO）；两个目标都放行时格式化与写出各做两次。
- 登记类接口（`create_logger` / `get_logger` / `destroy_logger` / `init` / `shutdown`）是冷路径，内部加锁，可能分配。
- 配置类接口（`enable_sink` / `disable_sink` / `set_sink_level` / `set_sink_path`）是冷路径，都不做日志格式化，
  但代价不同：
  - `set_sink_level`：只改目标阈值并同步后端阈值，不分配、不阻塞。
  - `enable_sink`：`Console` 目标**首次**启用时会**创建** sink 对象（可能分配）；`File` 目标只把已创建的目标
    重新挂载。未挂载时向 sink 列表追加（列表扩容时可能分配）；已挂载时幂等返回，不做 IO。
  - `disable_sink`：先 **flush** 该目标（可能阻塞在 IO），再摘除；不分配。
  - `set_sink_path`：创建文件目标（可能分配、可能阻塞在文件打开与父目录创建），挂载成功后 flush 并摘除旧目标
    （可能阻塞在 IO）。
  它们都不在日志写入路径上，也不与写入线程并发安全。
- 具体读数与评级见第 10 节与正式报告，本文件不转抄数字。

## 8. 版本与接口版本

- 底层日志库：spdlog 1.17.0（`vendor/spdlog`，MIT；fmt 许可在 `vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst`）。
- 集成方式：header-only；不编译 spdlog 静态库，不产生额外 `.lib`。
- 构建产物名：Windows/MSVC 为 `IDLog.dll`（导入库 `IDLog.lib`）；Windows/MinGW 为
  `libIDLog.dll`（导入库 `libIDLog.dll.a`）。
- 插件版本号与接口版本号：清单中尚无对应字段，**未冻结**；本文件不承诺任何修订兼容性。

## 9. 已知限制

- 公开头包含 vendor 内的 spdlog/fmt 头：消费者编译时间与目标文件体积上升是已知代价。
  所有模块必须使用本插件 `vendor` 里的同一份 spdlog，禁止另引其它版本（否则 ODR 违约）。
- 公开头在包含 vendor 前定义 `WIN32_LEAN_AND_MEAN` 与 `NOMINMAX`（仅在未定义时）。
- 消费者的翻译单元必须在包含任何 spdlog 头之前包含 `IDLog.hpp`（或 `IDLogCore.hpp`）：本插件的
  spdlog 编译期开关只在该头里设置一次，若 spdlog 已被先行包含，公开头会用 `#error` 拒绝编译，
  而不是让两套配置悄悄共存（ODR 违约）。同理，`SPDLOG_COMPILED_LIB`、`SPDLOG_USE_STD_FORMAT`、
  `SPDLOG_NAMESPACE`、`SPDLOG_WCHAR_FILENAMES`、`SPDLOG_NO_ATOMIC_LEVELS`、`SPDLOG_FMT_EXTERNAL`、
  `SPDLOG_FMT_EXTERNAL_HO` 也不得由消费者定义。
- 文件路径按窄字符传递：Windows 下受系统 ACP 影响，非 ACP 可表示的路径可能打不开。
- 文件目标的父目录不存在时由底层自动创建；因此"父目录可写但路径写错"仍会成功并新建目录，
  `set_sink_path` 返回 `true`。只有父目录不可创建（盘符不存在、无权限、父路径是普通文件等）
  才返回 `false`。
- 目标类型是运行期参数：非法的 `SinkType` 值不会被静默降级，四个配置方法都返回 `false`
  （见 2.3 节）。
- 级别也是运行期参数：`set_sink_level` 收到超出 `Level::Fatal` 的强制转换值时返回 `false` 且不改动
  任何阈值；插件不会把它归一成 `Trace` 或其它合法阈值（见 2.3、6 节）。
- `LoggerHandle` 不带代数/版本位：`shutdown` 之后重新 `init`，旧句柄数值可能被新日志器复用（ABA），
  `get_logger` 无法区分新旧日志器；调用方必须重新 `create_logger`（见 5、6 节）。
- 中文等非 ASCII 消息要求终端代码页为 65001（UTF-8）。库不修改进程级控制台设置。
- MSVC 组合：spdlog 1.17 内嵌 fmt 要求编译单元以 UTF-8 执行字符集编译（`/utf-8`）。
  当前工程未设置该选项，因此 MSVC 构建下本插件与任何包含其公开头的消费者都需要该选项，
  否则 fmt 会触发静态断言。GCC（MinGW）不受影响。
- 不使用 spdlog 的全局 registry / 默认 logger / 全局线程池：全部对象由本插件实例独占并显式释放。
- 未提供异步写，也未提供调用方可读的丢弃计数：同步写出，慢设备会直接反映为调用阻塞；
  运行期写失败的唯一报告是底层向 stderr 的限频消息（其中带一个累计计数），见第 6 节。

## 10. 验证状态

- 正确性与性能测试归 `Benchmark` 工程；本插件内不含测试入口。
- **MinGW GCC 16.2.0 / Release / SHARED / AMD64**：`Benchmark` 正式套件已出报告（`Benchmark/report.txt`）——
  IDLog 正确性 103 例通过、0 失败、0 跳过、3 项未覆盖（运行期写失败的可观测性、`init`/`shutdown` 成本、
  `shutdown` 后旧指针调用）。相对上一版 84 项判定不变，新增非法 `Level`（含与非法 `SinkType` 组合）、
  `shutdown`→`init` 句柄别名（ABA 现状固化）、`create_logger` 与 `shutdown`/`init` 竞态防御三组用例。
  性能 11 项指标已测量：7 项以场景预算目标评级（`basis=budget`，即 `grading.md` §2 E 的 `POLICY` 类
  500 ns/条参考值，不是理论下界、也不是同轮实测对照）——`world-class` 5、`excellent` 1、`good` 1、
  `fair` 0；4 项按规则不评级，因此报告 `ungraded=4`、官方退出码 2（`ungraded-metrics`）：
  `get_logger` 登记路径（budget 不适用于登记/查表类操作，且本配置无同轮同进程的直接调用对照）、
  `file.latency_p90`（上一数据块同变体跨进程 5 连跑极差超 `grading.md` §5 的 20% 噪声带，按已知缺口固定不评级）、
  `file.latency_p99`（尾部分位在多组跨进程 5 连跑中持续超同一带，固定标 `unstable` 不给比值评级）、
  `trim.partial_one_target`（当轮进程内 5 轮极差超同一带，标 `unstable`，按 §6 不当有效评级）；
  `trim.all_targets` 当轮未超带，正常评为 `world-class`。
  500 ns/条的推导式与全部假设见该报告 measurement notes 的 reference 推导节。
  噪声带由 budget 口径启用后的 5 连跑确认，逐次读数与极差表见
  `Benchmark/build/gcc16.2-release-shared/repeat-runs.txt`。
  具体读数与完整结论以该报告为准，本文件不转抄实测 ns 数字。
- **MSVC**：未验证。spdlog 1.17 内嵌 fmt 要求编译单元带 `/utf-8`，当前工程未设置该选项，构建被阻塞。
- **Linux 与非 Windows 平台**：未构建、未验证。
- **真实终端渲染成本**：未测（console 计时期间 stdout 重定向到 NUL，不含终端渲染）。
