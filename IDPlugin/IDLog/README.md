# ID.Log 使用说明（消费者版）

ID.Log 是 IDGameEngine 的日志插件：它以服务槽 `ID::ServiceSlot::Log`（数值 0）对外提供一张函数表，
宿主与下游插件通过这张表写日志、改配置、取诊断读数，不需要、也不允许把日志后端编进自己的模块。

本文面向**使用** ID.Log 的下游开发者：接口、用法、推荐方案、明确不建议的用法与性能预算边界。
插件内部的实现结构不在本文范围；需要改 ID.Log 本身时看工作台的 `skills/id-log/SKILL.md`。

- 当前接口修订：`ID::Log::kLogApiRevision = 2`（服务表六入口 + `active_level` + `revision`）
- 插件发布号：`plugin.json` 的 `version = 1`，能力名 `ID.Log`，槽位 0
- 语言基线：C++20；调用侧格式化用 `<format>`（编译期格式串）

---

## 1. 简介与兼容性

### 1.1 能力与访问方式

产品启动（`ID::Engine::start()` 返回 Ready）之后，服务槽 0 上发布一张 `ID::Log::LogApiTable`：

```cpp
const ID::Log::LogApiTable* table =
    ID::PluginRuntime::instance().get_service<ID::Log::LogApiTable>(ID::ServiceSlot::Log);
```

`get_service` 本身是一次固定下标 + 一次指针 load，不校验状态、类型与所有权；
它是否为空完全由「产品是否选中 `ID.Log`」和「当前是否在 Ready 区间」决定。

### 1.2 常量与硬上限

| 常量 | 值 | 含义 |
|---|---|---|
| `kLogApiRevision` | 2 | 接口修订；表里的 `revision` 必须等于它 |
| `kLogSlot` | 0 | 服务槽号，等于 `ID::ServiceSlot::Log` |
| `kMaxLoggerHandles` | 64 | 句柄槽位总数（插件自己在初始化时占 1 个，宿主可申请 63 个） |
| `kStackFormatCapacity` | 512 | 单条消息文本上限（字节）；超出按 UTF-8 边界截断并补 `...` |
| `kLineCapacity` | 1024 | 最终日志行上限（含 `[category] ` 前缀）；超出再截断并补 `...` |
| `kMaxFilePathBytes` | 260 | `LogConfig::file_path` 容量（含末尾 NUL，超长按容量截断） |

所有级别都编入产物：`ID_LOG_MIN_LEVEL` 在公开头里固定为 `0`，任何 `-D` 或构建定义覆盖都会在编译期报错。
不提供「按产品裁剪级别」的运行时开关。当前固定为 0，因此固定级别宏都会保留；只有将来改头文件基线并重新构建时，
低于基线的宏才会展开为 `((void)0)`。

### 1.3 兼容性矩阵

| 项 | 状态 |
|---|---|
| Windows + MinGW（GCC 16.2）+ SHARED | 已验证：Debug / Release 两配置均构建、运行、取过证据 |
| Windows + MSVC（19.52）+ SHARED | 已验证：Debug / Release 两配置均构建、运行、取过证据 |
| SHARED 四配置矩阵（MinGW/MSVC × Debug/Release） | 已验证（组装证据、编译宏、宿主退出码、终止语义校验、6 条性能指标齐全） |
| STATIC 归并（`ID_MONOLITHIC_BUILD=ON`） | **未验证**，采用前需另行确认（本插件的四配置证据都是 SHARED） |
| Clang、非 Windows 平台 | **未验证** |
| 安装后用 `find_package(IDGameEngine)` 消费 | 最小消费程序（start/shutdown）已验证（MinGW 与 MSVC）；带文件日志的安装后消费未单独验证 |
| 热重载插件、跨模块 STL/ABI 混用 | 未验证，且当前不支持 |

### 1.4 构建与包含前提

1. **产品必须选中 `ID.Log`**：profile 的 `enabled_plugins` 里要有 `"ID.Log"`（内置 `Assemble/profiles/log.json` 是唯一只含 IDLog 的示例）。
   仓库自带的 `dev` / `dev-mono` / `ship` preset 用的是 `default` profile（fixture 插件集合），**不含 `ID.Log`**，直接用它们取表会得到 `nullptr`。
2. **只链接产品消费目标**，不要单独链接 `IDLog`：

   ```cmake
   find_package(IDGameEngine REQUIRED)          # 必须在创建消费方 target 之前
   add_executable(Game src/main.cpp)
   target_link_libraries(Game PRIVATE IDGameEngine)
   ```

   公开头 `IDLogLogger.hpp`、`IDLogApi.hpp` 的 include 路径与身份宏 `ID_USE_PLUGIN_IDLOG`
   都由 `IDGameEngine → IDAssembly → IDLog` 的 PUBLIC usage requirement 传播，不要手写 include 路径或 `target_compile_definitions`。
3. **用 `#if defined(ID_USE_PLUGIN_IDLOG)` 保护可选包含**：同一份源码也可能在没选 `ID.Log` 的产品里编译；
   未选中时既没有公开头也没有身份宏。
4. 想拿到「有哪些级别、哪些字段」只读公开头：`#include "IDLogLogger.hpp"` 就够了（它包含 `IDLogApi.hpp`）。
   内核头、其它插件、宿主的 TU 都不需要、也不应该包含日志后端自身的任何实现头。

---

## 2. 快速开始

### 2.1 启动 →（可选）改配置 → 取句柄 → 写日志

```cpp
#include "IDGameEngine.hpp"      // ID::Engine::start/shutdown、PluginRuntime、ServiceSlot
#include "IDLogLogger.hpp"       // LogApiTable / LogConfig / LogStats / Logger / ID_LOG_* 宏

#include <cstdint>
#include <cstdio>

namespace
{
    // category 必须是静态存储期字符串：插件只保存指针，不复制内容
    constexpr char kCategory[] = "Game";

    const ID::Log::LogApiTable* g_log_table = nullptr;
    ID::Log::Logger*            g_logger    = nullptr;
}

bool game_start(const char* log_file) noexcept
{
    // 1) 启动产品；失败时运行期已回滚到 Idle，started.code / state 是结构化诊断
    const ID::PluginRuntimeDiagnostic started = ID::Engine::start();
    if (!started.is_ok())
    {
        return false;
    }

    // 2) Ready 区间内取服务表；revision 不匹配说明头文件与产物不是同一次构建
    g_log_table = ID::PluginRuntime::instance().get_service<ID::Log::LogApiTable>(ID::ServiceSlot::Log);
    if (g_log_table == nullptr || g_log_table->revision != ID::Log::kLogApiRevision)
    {
        static_cast<void>(ID::Engine::shutdown());
        return false;       // 产品没选中 ID.Log，或接口修订不一致
    }

    // 3) 需要文件 / 轮转才调 reconfigure（冷路径）。不需要就跳过：默认就是 console + async
    if (log_file != nullptr)
    {
        ID::Log::LogConfig config{};
        config.enable_file    = true;
        config.max_file_bytes = 16ull * 1024 * 1024;    // 16 MiB 轮转
        config.max_files      = 4;                      // 保留 4 个轮转文件
        std::snprintf(config.file_path, sizeof(config.file_path), "%s", log_file);
        if (!g_log_table->reconfigure(config))
        {
            ID::Log::LogStats stats{};
            g_log_table->get_stats(&stats);
            // stats.last_config_error 是 LogConfigError 的数值（见 3.6）；旧后端此刻仍然可用
            return false;
        }
    }

    // 4) 句柄：每个子系统只在这里申请一次并缓存，别在每帧、每实体里申请
    g_logger = ID::Log::acquire_logger(*g_log_table, kCategory);
    if (g_logger == nullptr)
    {
        static_cast<void>(ID::Engine::shutdown());
        return false;       // 槽位耗尽或 category 非法
    }

    // 5) 两种写法等价：成员方法（实参先求值）与固定级别宏（级别关闭时实参不求值）
    g_logger->info("game start: revision={}", g_log_table->revision);
    ID_LOG_INFO(g_logger, "game start via macro, entity={}", 1024);
    return true;
}
```

### 2.2 关闭

```cpp
void game_stop() noexcept
{
    // 1) 先停掉所有会写日志的线程 / 任务 / 作业队列（本产品不替调用方收敛生产者）
    // 2) 冷路径 flush：把已提交的消息推给输出，不保证落盘，也不等待异步队列排空
    if (g_log_table != nullptr)
    {
        g_log_table->flush();
    }

    // 3) 关闭产品：插件的 on_shutdown 会把 64 个句柄槽全部复位
    static_cast<void>(ID::Engine::shutdown());

    // 4) 句柄一律置空：shutdown 之后旧句柄的 table 变成 nullptr，再调用普通方法就是空指针崩溃
    g_logger    = nullptr;
    g_log_table = nullptr;
}
```

---

## 3. 接口清单

签名与字段名逐字来自 `IDPlugin/IDLog/include/IDLogApi.hpp` 与 `include/IDLogLogger.hpp`。

### 3.1 `LogApiTable`（六个入口 + 两个字段）

| 入口 | 签名 | 并发契约 | 行为要点 |
|---|---|---|---|
| `write` | `void(*)(uint32_t level, const char* category, uint32_t category_len, const char* message, uint32_t message_len) noexcept` | **可由多线程并发调用** | 提交已格式化好的消息；`category`/`message` 允许 `nullptr`、不要求 NUL、不接管所有权；越界 `level` 收敛为 `Error`；`Level::Fatal` 无条件终止 |
| `configure` | `bool(*)(const LogConfig&) noexcept` | 不得与 `write`/`flush`/`reconfigure`/`get_stats`/shutdown 并发 | 只接受 `min_level` 与 `flush_interval_ms`；拓扑字段有任何差异返回 `false` 且不改状态 |
| `flush` | `void(*)() noexcept` | 不得与 shutdown 并发 | 异步形态只投递 flush 屏障、**不等待完成**；不代表队列已排空，也不是 `fsync` |
| `acquire_logger` | `Logger*(*)(const char* category, uint32_t category_len) noexcept` | 冷路径，只在 Ready 区间调用 | 无锁、无分配、不做字符串查重；`category == nullptr`、表项为空或槽位耗尽返回 `nullptr` |
| `reconfigure` | `bool(*)(const LogConfig&) noexcept` | 不得与 `write`/`flush`/`configure`/`get_stats`/shutdown 并发 | 全部字段都可改（重建后端）；失败时旧后端与旧状态完全不变；成功时可能阻塞到旧队列排空 |
| `get_stats` | `void(*)(LogStats*) noexcept` | 不得与 `reconfigure`/shutdown 并发 | `nullptr` 时什么都不做；无后端时清零只留 `ready=false` 与 `last_config_error` |
| `active_level` | `const std::atomic<uint32_t>*` | 只读，任何时刻安全 | 级别过滤的唯一真源；调用方预检读的就是它（relaxed） |
| `revision` | `uint32_t` | 常量 | 必须等于 `kLogApiRevision`（当前 2） |

`reconfigure` 不重建句柄：表与槽位不变，已有 `Logger*` 继续有效。
`configure` / `reconfigure` / `get_stats` / `flush` / shutdown 之间「不并发」是**宿主的责任**，插件不加锁兜底。

### 3.2 `Logger`

`Logger` 是可平凡复制的纯数据，由插件在固定槽位里分配；调用方只持有指针，不自己构造、不拷贝后长期保存。

| 成员 | 形式 | 说明 |
|---|---|---|
| `table` / `category` / `category_len` | public 字段 | 插件填充；`category` 指针由调用方保证在句柄有效期内不释放 |
| `is_enabled(Level)` | 内联方法 | 一次 relaxed load + 一次分支；**不判空**、不加锁、不查字符串 |
| `write_at(Level, fmt, args...)` | 方法 | 只做格式化 + 提交，**不做级别预检**；确知该级别已打开时才用 |
| `trace` / `debug` / `info` / `warn` / `error` | 方法 | 内部先做 `is_enabled` 再格式化；**实参在调用前必然已求值** |
| `fatal` | `[[noreturn]]` 方法 | 不受过滤；`table` 为空时走自举日志回退后终止；只在不可恢复时用 |

行形态（console 写 stderr，无颜色）：`[日期 时间] [级别] [线程 id] [category] 消息`，
例如 `[2026-10-21 16:10:24.123] [info] [1676] [Game] game start: revision=2`。
`Level::Fatal` 的行标签显示为 `critical`（后端档位名），不是 `fatal`。

### 3.3 宏

| 宏 | 用途 | 级别关闭时 |
|---|---|---|
| `ID_LOG_TRACE/DEBUG/INFO/WARN/ERROR(logger_ptr, fmt, args...)` | 固定级别的快路径，实参可能昂贵时用它 | 编译期底线以下展开为 `((void)0)`；运行期级别关闭时同样**完全不求值** |
| `ID_LOG_AT(logger_ptr, level, fmt, args...)` | 级别是运行期变量时用 | 走完整检查（先收敛越界值、再优先判 Fatal）；`Fatal` 走必终止路径 |
| `ID_LOG_FATAL(logger_ptr, fmt, args...)` | 不可恢复错误 | 不参与任何过滤，先输出再终止；`logger_ptr` 为 `nullptr` 也照样终止 |

- 宏里 `logger_ptr` 与动态 `level` 都只求值一次，可以传带副作用的表达式。
- `ID_LOG_AT_FIXED` 是上表前五个宏内部使用的快路径，**不要直接调用**（它要求级别是编译期枚举常量）。

### 3.4 `LogConfig`（按值初始化，按 `const LogConfig&` 传入）

| 字段 | 类型 | 默认值 | 语义与校验 |
|---|---|---|---|
| `min_level` | `uint32_t` | `static_cast<uint32_t>(Level::Info)` | 唯一过滤级别；`> Level::Fatal` 视为非法（`InvalidLevel`） |
| `flush_interval_ms` | `uint32_t` | `0` | 0 = 不做周期 flush；非 0 时该后端带一个周期 flush 线程 |
| `queue_size` | `uint32_t` | `8192` | 只在 `async == true` 时校验：非 0 且是 2 的幂（`InvalidQueueSize`） |
| `max_files` | `uint32_t` | `0` | 轮转保留的轮转文件数（不含当前主文件）；与 `max_file_bytes` 同时非 0 才启用轮转 |
| `max_file_bytes` | `uint64_t` | `0` | 单文件字节上限 |
| `enable_console` | `bool` | `true` | 输出到 stderr |
| `enable_file` | `bool` | `false` | 需要 `file_path`；与 `enable_console` 同时为 false 时返回 `NoSink` |
| `async` | `bool` | `true` | 异步提交（调用线程只入队） |
| `overrun_drop_oldest` | `bool` | `true` | `true`：队列满覆盖最旧一条（不等待空位，但仍会短暂取队列自身锁）；`false`：队列满时生产者等待空位 |
| `flush_on_warn` | `bool` | `true` | `warn` 及以上触发一次 flush；高频 warn 场景可评估关闭 |
| `file_path` | `char[260]` | 全 0 | 必须以 NUL 结尾；插件不信任调用方，会把末字节强制写成 `'\0'`（等价于按容量截断） |
| `struct_size` | `uint32_t` | `sizeof(LogConfig)` | 0 或与本构建精确相等，否则 `InvalidEnvelope` |
| `config_revision` | `uint32_t` | `kLogApiRevision` | 0 或与本构建精确相等，否则 `InvalidEnvelope` |

两种改法分工固定：

- `configure`：只改 `min_level` / `flush_interval_ms`。其余字段（含与当前不同的 `file_path`）任何差异都返回 `false`，
  并记 `TopologyRequiresReconfigure`；被拒请求里的 `min_level` 也不会被顺手应用，不会创建/探测文件。
- `reconfigure`：全部字段，等价于重建后端。先完整校验（含文件可写探测）再建新后端，成功后才切换；
  任一步失败返回 `false` 且旧后端与旧状态不变。只改 `min_level` / `flush_interval_ms` 时它等价于 `configure`，不重建后端。

### 3.5 `LogStats`（调用方分配，插件写入）

| 字段 | 类型 | 含义 |
|---|---|---|
| `overrun_dropped` | `uint64_t` | 队列满时被覆盖的最旧消息数（当前后端的计数；`overrun_drop_oldest=true` 时的丢弃量） |
| `discarded` | `uint64_t` | 显式丢弃的新消息数（当前实现用「丢最旧」，通常为 0） |
| `dropped_before_ready` | `uint64_t` | 从未 Ready 过就进来的写入数（进程级累计，跨 start/shutdown 不归零） |
| `dropped_after_shutdown` | `uint64_t` | 曾经 Ready 过、后端已销毁后进来的写入数（进程级累计） |
| `queue_capacity` | `uint32_t` | 异步队列容量；同步形态为 0 |
| `active_level` | `uint32_t` | 当前过滤级别（与 `active_level` 原子量同源） |
| `last_config_error` | `uint32_t` | 最近一次 `init`/`configure`/`reconfigure` 的 `LogConfigError` 数值 |
| `ready` | `bool` | 后端是否可用 |
| `async` / `console` / `file` | `bool` | 当前拓扑 |

读 `overrun_dropped` 会取一次队列锁，因此 `get_stats` 是冷路径，必须与 `reconfigure` / shutdown 串行。
后端销毁后 `overrun_dropped` / `discarded` 归零（它们是当前后端的计数），两个进程级丢弃计数不受影响。

### 3.6 `LogConfigError`

| 值 | 名称 | 触发条件 |
|---|---|---|
| 0 | `None` | 上一次配置调用成功 |
| 1 | `NotReady` | Ready 区间之外调用 `configure` / `reconfigure`（此时没有后端可改） |
| 2 | `AlreadyReady` | 重复初始化 |
| 3 | `InvalidEnvelope` | `struct_size` / `config_revision` 与本构建不匹配 |
| 4 | `InvalidLevel` | `min_level` 越界 |
| 5 | `InvalidQueueSize` | 异步形态下 `queue_size` 为 0 或不是 2 的幂 |
| 6 | `MissingFilePath` | 开了文件输出却没有路径 |
| 7 | `FileNotWritable` | 文件路径不可写（建后端前探测 fopen 失败） |
| 8 | `NoSink` | `enable_console` 与 `enable_file` 都为 false |
| 9 | `TopologyRequiresReconfigure` | `configure` 收到拓扑字段差异，必须改用 `reconfigure` |

### 3.7 `acquire_logger` 的两个模板

```cpp
template <size_t N>
ID::Log::Logger* acquire_logger(const ID::Log::LogApiTable& table, const char (&category)[N]) noexcept;

template <size_t N>
ID::Log::Logger* acquire_logger(const ID::Log::Logger& owner, const char (&category)[N]) noexcept;
```

category 必须是编译期长度已知的字符数组（字符串字面量或静态存储期常量）；模板按数组长度算出字节数，不调用 `strlen`；第二个模板只借用 `owner.table`。
两者都是冷路径：无锁、无分配、不做 category 查重——**同一 category 申请两次会占两个槽位**。

---

## 4. 用法示例

以下示例共用第 2 节的包含与前置状态：`#include "IDGameEngine.hpp"` / `#include "IDLogLogger.hpp"` / `<cstdio>`，
产品已进入 Ready。示例里的 `table` / `logger` 指第 2 节的 `g_log_table` / `g_logger`（非空）；
`build_snapshot_text()` 代表任意昂贵函数，只用来表现求值时机。

### 4.1 console + async（启动默认形态，零配置）

产品初始化时插件已经用默认 `LogConfig{}` 建好后端：console + async、`queue_size=8192`、`min_level=Info`。
宿主什么都不做就能写：

```cpp
// 片段：peer_id / rtt_ms 是调用方已声明的局部变量
logger->info("connected: peer={} rtt_ms={}", peer_id, rtt_ms);
ID_LOG_WARN(logger, "packet loss spiked: {}%", loss_percent);
```

### 4.2 文件 + 轮转 + async（生产推荐）

```cpp
bool log_to_rotating_file(const ID::Log::LogApiTable& table, const char* path) noexcept
{
    ID::Log::LogConfig config{};                        // 先取默认值，再改成目标形态
    config.enable_file    = true;                       // 默认已开 console；需要纯文件输出时再置 false
    config.async          = true;
    config.queue_size     = 8192;                       // 必须是 2 的幂
    config.max_file_bytes = 32ull * 1024 * 1024;        // 32 MiB
    config.max_files      = 5;                          // 保留 5 个轮转文件
    std::snprintf(config.file_path, sizeof(config.file_path), "%s", path);

    if (table.reconfigure(config))
    {
        return true;
    }

    ID::Log::LogStats stats{};
    table.get_stats(&stats);                            // stats.last_config_error 给出原因，旧后端仍可用
    return false;
}
```

前提：`path` 是可写路径。`reconfigure` 在建后端前会做一次追加模式探写，不可写直接返回 `FileNotWritable`，不会动旧后端。

### 4.3 低频文件 + 同步（例如启动/退出各写几行）

```cpp
bool log_to_file_sync(const ID::Log::LogApiTable& table, const char* path) noexcept
{
    ID::Log::LogConfig config{};
    config.enable_console = false;    // 同步写已经够慢，通常不必再复制一份到 stderr
    config.enable_file    = true;
    config.async          = false;    // 写入发生在调用线程
    config.queue_size     = 0;        // 同步形态不看队列容量
    config.max_file_bytes = 0;        // 0 = 不轮转
    config.max_files      = 0;
    std::snprintf(config.file_path, sizeof(config.file_path), "%s", path);

    return table.reconfigure(config);
}
```

前提：调用点不在帧循环、不在热路径。同步形态的单行代价见第 7 节，量级是「毫秒每行」，不是微秒。

### 4.4 运行期改级别（`configure`）

```cpp
// 前提：没有任何线程正在写日志；configure 必须与 write / flush / reconfigure / get_stats / shutdown 串行
bool set_log_level(const ID::Log::LogApiTable& table, ID::Log::Level level) noexcept
{
    ID::Log::LogConfig config{};
    config.min_level = static_cast<ID::uint32_t>(level);
    if (table.configure(config))
    {
        return true;
    }

    ID::Log::LogStats stats{};
    table.get_stats(&stats);          // 典型值：InvalidLevel(4) 或 TopologyRequiresReconfigure(9)
    return false;
}
```

**前提是把拓扑字段填成当前值**。上面的 `LogConfig{}` 只在「当前仍是启动默认形态（console + async、queue 8192、无文件）」时成立；
如果此前 `reconfigure` 成文件输出，必须把 `enable_console` / `enable_file` / `async` / `queue_size` /
`overrun_drop_oldest` / `flush_on_warn` / `max_file_bytes` / `max_files` / `file_path` 全部填成当前值
（可用 `get_stats` 的 `async` / `console` / `file` / `queue_capacity` / `active_level` 核对），否则会得到 `TopologyRequiresReconfigure`。

### 4.5 昂贵参数：宏或 `is_enabled`

```cpp
// 片段：entity_count / build_snapshot_text() 由调用方声明
// 写法 A：先判级别，再算昂贵实参
if (logger->is_enabled(ID::Log::Level::Debug))
{
    logger->debug("snapshot: entities={} text={}", entity_count, build_snapshot_text());
}

// 写法 B：固定级别宏，级别关闭时实参完全不求值
ID_LOG_DEBUG(logger, "snapshot: entities={} text={}", entity_count, build_snapshot_text());
```

`logger->debug(...)` 是函数调用，**实参在进入方法前一定已经算过**；要严格零求值只能用宏。
两者都不改变「级别打开时的代价」，只是把关闭级别的代价压到可忽略（见第 7 节关闭级别读数）。

### 4.6 `Fatal`：只用于不可恢复

```cpp
// 前提：确实没有恢复路径（例如必需的设备对象为空，继续运行只会产生错误数据）
if (render_device == nullptr)
{
    ID_LOG_FATAL(logger, "render device lost: slot={}", static_cast<int>(ID::ServiceSlot::Render));
}
```

- `Fatal` 会先输出再终止进程，不回退、不被过滤级别裁掉。
- 可恢复失败（文件打不开、资源缺失但能降级）用 `error` / `warn` + 返回值，不要用 `Fatal`。
- `logger` 为 `nullptr` 或句柄已失效时，`ID_LOG_FATAL` 仍会先输出再终止，不会静默跳过。

### 4.7 关闭顺序

```cpp
// 1) 停掉所有会写日志的生产者（工作线程、作业队列、网络回调）
// 2) flush：只是把已提交内容推到输出，不等待异步队列排空，也不是落盘保证
g_log_table->flush();
// 3) 关闭产品（逆拓扑序 shutdown/unload；本插件会复位 64 个句柄槽）
static_cast<void>(ID::Engine::shutdown());
// 4) 句柄置空，之后不再使用
g_logger    = nullptr;
g_log_table = nullptr;
```

### 4.8 诊断读数

```cpp
void report_log_stats(const ID::Log::LogApiTable& table) noexcept
{
    ID::Log::LogStats stats{};
    table.get_stats(&stats);          // 冷路径；必须与 reconfigure / shutdown 串行

    if (!stats.ready)
    {
        // stats.last_config_error 说明最近一次失败的配置原因；此处后端不可用
        return;
    }

    // 队列压力：overrun_dropped 增长说明生产速率长期高于输出速率
    // dropped_before_ready / dropped_after_shutdown 非 0 说明存在过早或过晚的写入
    // queue_capacity / active_level / async / console / file 是当前拓扑
}
```

定期采样的字段建议只取计数差值（`overrun_dropped` 的增量），不要把每秒 `get_stats` 放进帧循环。

---

## 5. 建议使用方案

**构建形态**

1. Release 用异步（`async = true`，默认）；同步形态只在明确的低频场景（启动/退出几行）使用。
2. 开发期用 console + async（默认配置，零配置可用）；生产用文件 + 轮转 + async，并保留 console 便于现场排障。
3. 级别用 `Info` 起步、告警用 `Warn`；`Debug` 通过宏或 `is_enabled` 保护，发布构建默认关闭。

**句柄与 category**

1. 每个子系统（而不是每个类、每个实体）在 Ready 区间申请**一次**句柄，缓存到子系统对象里复用。
2. category 用字符串字面量或静态存储期常量；不要用临时 `std::string::c_str()`。
3. 全产品只有 63 个可用槽位；不要在每帧、每场景、每实体里 `acquire_logger`，也不要用完不回收地反复申请。

**队列与输出**

1. `queue_size` 取 8192 或更大的 2 的幂；容量越大越能吸收 burst，但只推迟丢弃时间，不提高输出能力。
2. 保持 `overrun_drop_oldest = true`（默认）以避免生产者等待队列空位；它仍会取队列锁，不是绝对不阻塞。同时监控 `overrun_dropped` 判断是否长期跟不上。
3. 需要严格不丢日志时不能只靠日志后端（见第 6 节），要自己在业务侧做落盘/审计通道。

**冷路径操作**

1. `flush()` 只在关闭、崩溃边界、明确的检查点调用。
2. `get_stats`、`configure`、`reconfigure` 都是冷路径，放在初始化、场景切换、现场诊断里，不要放进帧循环。
3. 改级别用 `configure`，并且保证当时没有并发写者（暂停写者或放启动期）。
4. 消息体尽量保持在 250 字节以内：短消息走内联缓冲，不额外分配（见 7.4 第 3 条）。

---

## 6. 不建议 / 高开销用法

| 不建议 | 原因 | 替代做法 |
|---|---|---|
| 游戏热路径使用同步模式 | 同步写入发生在调用线程。实测「stderr→文件」约 **0.9 ms/行**（MinGW 0.92、MSVC 0.87）：60 fps 下每帧 1 行就占约 5.5% 帧预算，10 行约 55%，且这是 sink 写磁盘句柄的代价，真控制台是另一套成本 | 用 async；确需同步的日志放加载/退出等冷路径 |
| 每帧逐实体打日志 | 单条 async 生产者代价约 154–452 ns（见第 7 节），乘上实体数会直接把帧预算吃掉；同时挤爆队列 | 帧末汇总成 1 条，或改用计数器/指标 |
| 热循环里打 `Debug`/`Trace` | 即使级别关闭，方法形态仍会求值实参；打开时更慢 | 用 `ID_LOG_DEBUG` 等宏，或先 `is_enabled` 再算参数 |
| 长 payload（数十 KB 的 JSON/dump）每帧输出 | 超过约 250 字节可能多一次堆分配；超 512 字节还会被截断并补 `...`，信息本身也丢 | 短消息 + 需要时输出文件/单独通道；大对象按需 dump |
| 昂贵格式化参数未先判级别 | `logger->debug("{}", serialize_scene())` 会先执行 `serialize_scene()` | 宏或 `is_enabled` 保护 |
| 把 `flush()` 当落盘保证、每帧调用 | 异步形态下 `flush` 只投递屏障、不等待、不排空队列；`fflush` 不是 `fsync` | 关闭/崩溃边界调用；持久化需求用文件写入 + 自己的同步逻辑 |
| `get_stats` / `configure` / `reconfigure` 与写线程并发 | 契约要求宿主串行；并发时行为不在保证范围内 | 停写者、放启动期或场景切换点 |
| `reconfigure` 时仍有大量写者 | 重建后端会销毁旧队列，且可能阻塞到旧队列排空 | 切配置前暂停生产者 |
| `Fatal` 报可恢复错误、把日志当审计/安全记录 | `Fatal` 直接终止进程；日志后端没有防篡改、没有事务、异步形态允许丢最旧 | 可恢复用 `error`/`warn` + 返回值；审计走独立可靠通道 |
| 依赖队列满时「一条不丢」 | 默认 `overrun_drop_oldest` 覆盖最旧；即使改成阻塞策略也只是把压力转移给生产者 | 接受丢弃并用 `overrun_dropped` 观测，或降低产出速率 |
| 跨 `shutdown` 保留并使用旧句柄 | 插件 `on_shutdown` 复位全部槽位，旧句柄 `table == nullptr`；普通方法解引用即崩溃 | 把句柄置空，下一轮 `start` 后重新申请 |
| 用临时 category 指针申请句柄 | 插件只保存指针不复制内容 | 用字面量或静态存储期常量 |
| 超过 64 个句柄 | 初始化已占 1 个，可申请 63 个，耗尽后返回 `nullptr` | 每个子系统一个句柄，长期复用 |
| 日志目录不可写 / 磁盘满 | 建后端前有探写，但运行期写失败在无异常构建里没有异常可抛，极端情况会终止进程 | 部署时保证目录可写与磁盘余量，失败路径不要依赖日志 |

**关于「一条不丢」**：本插件的定位是**诊断日志**。它不提供投递保证、不做持久化同步、不做审计级顺序保证；
需要这些语义时必须另建通道，日志只作为旁路观测。

---

## 7. 性能预算

### 7.1 口径（先看这一段）

> **本节数字是 per-call 实测中位数 + 算术外推，不是端到端吞吐测试，也不是性能评级结论。**

- 数字来源：
  - 官方 primary 配置（MinGW Release）：`Benchmark/report.txt`（等于 `Benchmark/build/log-rev2/final3-mingw-release-report.txt`）
  - MSVC Release：`Benchmark/build/msvc-log-rev2/final3-msvc-release-report.txt`
  - 四配置等级：`Benchmark/build/verification-matrix.txt`
- 每条指标都是**单次调用**的中位数（每次运行 5 轮取中位数）；`basis=regression`：参考值是同配置基线
  （三次运行的中位数之中位数），`ratio` 落在噪声带内只表示「没有相对基线退化」，不是绝对质量评价。
- 打开级别（`enabled`）指标测的是**生产者侧代价**：调用侧格式化 + 一次跨 DLL 间接调用 + 入队（异步）
  或直接写 sink（同步）。**不含** 后台写出线程的成本，因此不能换算成端到端吞吐。
- 所有绝对 ns 都在「自动化运行把进程 stderr 重定向到文件」这一 sink 目标下取得；
  **真控制台未测**，不能与真控制台或其他 sink 目标跨轮比较。
- 表中 `unstable` 标记表示该指标的 5 轮极差超过 20%，只能做量级参考。

### 7.2 per-call 实测（ns/次）

| 指标 | MinGW Release | MSVC Release | 含义 |
|---|---|---|---|
| `log.disabled.method.ns` | 0.34（fair） | 0.46（fair） | 级别关闭，成员方法形态（实参仍求值） |
| `log.disabled.macro.ns` | 0.38（good） | 0.48（fair） | 级别关闭，固定级别宏（实参不求值） |
| `log.enabled.short.ns` | 153.96（good） | 170.33（good） | 级别打开，短消息（payload ≈ 33 B），异步入队 |
| `log.enabled.long.ns` | 192.58（good） | 452.08（good） | 级别打开，长消息（payload ≈ 305 B > 250 B 内联容量），异步入队 |
| `log.flush.ns` | 182.71（good） | 118.49（fair） | 队列已排空时的一次 `flush()`（不等待屏障完成） |
| `log.enabled.sync.ns` | 916110.94（good，unstable 158%） | 867098.44（good） | 级别打开，同步模式（stderr=file），payload ≈ 33 B |

四配置等级（`good` / `fair` / `excellent`，来自 `verification-matrix.txt`）：

| 配置 | method | macro | short | long | flush | sync |
|---|---|---|---|---|---|---|
| MinGW Release（官方） | fair | good | good | good | good | good |
| MSVC Release | fair | fair | good | good | fair | good |
| MinGW Debug | good | good | good | good | excellent | good |
| MSVC Debug | good | good | good | good | fair | good |

本插件四配置里出现的 `fair` 是比同配置参考值慢约 20–35%，不代表正确性失败，也不代表「不可用」；
四配置的 6 条指标都没有出现 `poor` / `terrible` / `ungraded`。**不要把 `fair` 写成 `good`，也不要把 sync 的读数当成真控制台成本**。

### 7.3 算术外推（生产者侧，单核时间占比）

以 MinGW Release 的短消息 153.96 ns/条、长消息 192.58 ns/条、MSVC Release 的 170.33 / 452.08 ns/条做线性外推
（只看生产者侧；不含后台写出线程的成本）：

| 速率 | 短消息 MinGW | 短消息 MSVC | 长消息 MinGW | 长消息 MSVC |
|---|---|---|---|---|
| 1k 条/s | 0.15 ms/s ≈ 0.015% | 0.17 ms/s ≈ 0.017% | 0.19 ms/s ≈ 0.019% | 0.45 ms/s ≈ 0.045% |
| 10k 条/s | 1.54 ms/s ≈ **0.15%** | 1.70 ms/s ≈ 0.17% | 1.93 ms/s ≈ 0.19% | 4.52 ms/s ≈ 0.45% |
| 50k 条/s | 7.70 ms/s ≈ 0.77% | 8.52 ms/s ≈ 0.85% | 9.63 ms/s ≈ 0.96% | 22.6 ms/s ≈ 2.3% |
| 100k 条/s | 15.4 ms/s ≈ **1.5%** | 17.0 ms/s ≈ 1.7% | 19.3 ms/s ≈ **1.9%** | 45.2 ms/s ≈ **4.5%** |
| 1M 条/s | 154 ms/s ≈ **15.4%** | 170 ms/s ≈ 17.0% | 193 ms/s ≈ 19.3% | 452 ms/s ≈ 45.2% |

关闭级别（宏形态，MinGW 0.38 ns / MSVC 0.48 ns）：1M 条/s 约 0.38–0.48 ms/s，可以忽略；
但前提是**用宏或先判 `is_enabled`**，方法形态关闭时仍会求值实参。

同步模式（stderr→文件，约 0.9 ms/行）：

| 场景 | MinGW（0.916 ms/行） | MSVC（0.867 ms/行） |
|---|---|---|
| 60 行/s | 55.0 ms/s ≈ 5.5% 单核 | 52.0 ms/s ≈ 5.2% 单核 |
| 60 fps 每帧 1 行 | 0.92 ms ≈ 5.5% 帧预算（16.7 ms） | 0.87 ms ≈ 5.2% 帧预算 |
| 60 fps 每帧 10 行 | 9.16 ms ≈ 55% 帧预算 | 8.67 ms ≈ 52% 帧预算 |

### 7.4 决定实际上限的因素（这些没有测）

上面的 per-call 数字只覆盖调用线程。真实吞吐上限由以下因素决定，**均未在本次证据中测量**：

1. 输出端本身的带宽：后台写出线程写文件、轮转重命名、真控制台刷新、磁盘延迟。
2. 队列容量与丢弃策略：`queue_size = 8192`、`overrun_drop_oldest = true` 时的稳定吞吐与丢弃比例。
3. 长消息的额外堆分配：payload 超过约 250 字节时异步路径会多一次分配。
4. 多生产者竞争：多线程同时写同一条队列时的锁竞争与缓存行争用。
5. `flush_on_warn = true` 时 warn 及以上带来的额外 flush 频率。

因此：**不要把 7.3 的外推当成「本插件能跑 1M 条/s」的结论**；那是「只算生产者、不算输出」的上界估计。

---

## 8. 中大型游戏适用性结论

- **可以支撑**：中大型游戏在 Release + async 下的常规诊断日志量（约 1k–50k 条/s）。
  按 50k 条/s 外推，生产者侧约占单核 0.8%（短消息，MinGW）。
- **不建议**：100k 条/s 以上；每帧大量日志（尤其是逐实体）；同步模式进热路径；每帧输出长 payload/JSON。
  这些首先会把队列打满（丢最旧），其次把帧预算吃掉，最后才轮到 CPU 百分比。
- **高频遥测不是日志**：帧时间、实体数、内存曲线这类每秒数百至数万点的数据走 metrics/计数器通道，
  日志只留关键事件与异常。把遥测塞进日志会同时污染信号与性能。
- **文件高吞吐需要另行评估**：本仓库只测了 per-call 代价与 stderr→文件 的单次写入代价；
  持续 MB/s 级落盘、轮转压力、批量写、原生后端（如直接 `WriteFile`/异步 IO）都需要单独测量后再决定。
- **不能宣称**：硬实时有界、审计级完整、崩溃后一定留下最后一行（异步在途消息可能丢）、
  STATIC 归并下的同样性能（未验证）、Clang/非 Windows 上的同样行为（未验证）。

一句话：**ID.Log 是诊断日志，不是数据通道。** 需要「不丢、可审计、可回放」的场合另建通道。

---

## 9. 维护说明

本文的读者是**消费者**，不是插件维护者，所以这里不写后端内部结构与实现取舍。

- 修改范围：公开接口（`LogApiTable` / `Logger` / 宏）、`LogConfig` / `LogStats` / `LogConfigError` 的字段与默认值、
  生命周期与线程契约、`Fatal` 与 `flush` 语义、性能建议与预算数字，都必须在同一改动里同步更新本文。
- 数字必须有依据：性能数字只能来自 `Benchmark` 的官方报告与四配置矩阵，禁止估算冒充实测；
  口径（per-call 中位数、`basis=regression`、stderr→文件）必须与报告一致。
- 示例必须有前提：每段示例写清线程/时序前提（Ready 区间、串行要求、拓扑一致性），并保持可编译。
- 兼容性状态只有真实取过证据才能改：某平台/模式没有证据时写「未验证」，不要写成「已支持」。
- 下游验证过更好的用法时，先向插件 owner 提证据，由 owner 复验后更新本文；不要直接改本文件。

---

## 10. 版本与修订

| 项 | 值 |
|---|---|
| 接口修订 `kLogApiRevision` | 2 |
| 服务槽 | `ID::ServiceSlot::Log` = 0 |
| 插件发布号（`plugin.json` 的 `version`） | 1 |
| 能力名 | `ID.Log` |
| 服务表形态 | 六入口（`write` / `configure` / `flush` / `acquire_logger` / `reconfigure` / `get_stats`）+ `active_level` + `revision` |

下游必须校验 `table->revision == ID::Log::kLogApiRevision`；不等时说明产物与头文件不是同一次构建，应视为不可用。
