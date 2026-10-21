#include "benchmark.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <io.h>
    #include <process.h>     // _getpid：临时文件名的进程唯一后缀
#else
    #include <unistd.h>      // getpid
#endif

// 只有选中 ID.Log 的产品构建才拿得到这两个头：身份宏 ID_USE_PLUGIN_IDLOG 由 IDLog target 的
// PUBLIC usage requirement 经 IDAssembly -> IDGameEngine 传播，include 目录同理。
// 因此项目头必须放在条件编译内，否则默认/空 profile 构建会直接编译失败。
#if defined(ID_USE_PLUGIN_IDLOG)
    #include "IDGameEngine.hpp"
    #include "IDLogLogger.hpp"
#endif

/**
 *  ID.Log 正确性测试（rev2：六函数服务表 + 运行期诊断 + 后端重建）
 *  1. 被测面是 `ID::Engine::start()/shutdown()` 与 `ServiceSlot::Log` 上的 `LogApiTable` rev2：
 *     write / configure / flush / acquire_logger / reconfigure / get_stats + active_level + revision；
 *  2. `configure` 只接受 `min_level` / `flush_interval_ms`；拓扑差异必须被拒并把
 *     `last_config_error` 置为 `TopologyRequiresReconfigure`，且被拒请求里的 `min_level` 不得被顺手应用；
 *  3. `reconfigure` 是冷路径：先完整校验（含文件可写探测）再建新后端，成功后才交换；失败时旧后端与旧状态
 *     完全不变。异步 → 同步、同步 → 异步、console → 文件 sink 的往返都在本域真实执行；
 *  4. 落盘类断言全部读回真实文件，不把"调用返回"当成证据：
 *     - 截断：拼装层 `kLineCapacity`(1024) 截断后补 "..."，UTF-8 多字节序列被切断时向前回退到码点边界；
 *     - 边界：payload 恰好 1024 字节不截断，多请求 1 字节则截断到 1024 且以 "..." 结尾；
 *     - 非法 level(999)：前端收敛为 Error，落盘行的 `%l` 必须是 `error`，且进程不 abort；
 *     - nullptr 段与零长段不写任何字节（payload 只剩 `[] ` 或 `[category] `）；
 *     - 4 线程 × 100 条同步写入必须落盘 400 行（每线程 100 行）；
 *     - 轮转：小 `max_file_bytes` + `max_files` 下主文件不超上限、base/.1/.2/.3 都存在且末尾行仍在主文件、
 *       max_files+1 的 `.4` 必须不存在（前提：spdlog 先轮转后写、轮转名 `<stem>.<n><ext>`，n 从 1 起）；
 *     - console sink：进程内临时重定向 stderr 后重建后端，再读回 `[info]` 行（Windows 上 sink 在构造时
 *       缓存 `_get_osfhandle(_fileno(stderr))`，所以重定向必须发生在建 sink 之前）；恢复 stderr 时
 *       检查 `_dup2` 返回值，并用"向 stderr 写探针后捕获文件长度不变"证明已指回原目标（不比较句柄数值：
 *       同一目标在 Windows 上被 `_dup2` 复制后句柄值会变），失败时由 detail 点名；
 *  5. `Level::Fatal` 无条件终止、shutdown 后解引用旧句柄是契约外空指针、并发 shutdown 属宿主契约：
 *     三条都只能子进程隔离验证，本进程只 Skip 并写明依据（validation/log 的子进程用例覆盖前两条）；
 *  6. 未选中 ID.Log 的 profile 下同样有实现：只输出一条 Skip，保证域清单与伞形头不随 profile 消失；
 *  7. 所有输出走 `ID::Bench`；DUT 自己的 console sink 写 stderr，不进 report.txt，也不参与 console/report 比对；
 *  8. 临时日志文件一律落在 `ID_BENCH_LOG_TMP_DIR`（CMake 注入的构建目录绝对路径），与 CWD 无关；
 *     文件名带当前进程号，同一构建目录被两个进程并发跑时互不覆盖，清理也只删自己的文件。
 */

// 兜底：直接用编译器单独编译本文件时也要能编过（正式构建由 CMake 注入构建目录）
#if !defined(ID_BENCH_LOG_TMP_DIR)
    #define ID_BENCH_LOG_TMP_DIR "."
#endif

#if defined(ID_USE_PLUGIN_IDLOG)

namespace
{
    using ID::Log::kLineCapacity;
    using ID::Log::kLogApiRevision;
    using ID::Log::kMaxLoggerHandles;
    using ID::Log::kMaxFilePathBytes;
    using ID::Log::LogApiTable;
    using ID::Log::LogConfig;
    using ID::Log::LogConfigError;
    using ID::Log::LogStats;
    using ID::Log::Logger;

    constexpr char         kCategory[]     = "Bench.Log";
    constexpr ID::uint32_t kCategoryBytes  = sizeof(kCategory) - 1;

    constexpr char         kFileCategory[]     = "Bench.Log.File";
    constexpr ID::uint32_t kFileCategoryBytes  = sizeof(kFileCategory) - 1;

    // 临时日志文件统一落在 CMake 注入的构建目录：早先写相对路径会把文件落到当前工作目录
    // （可能是仓库根），既不干净、也让读取路径依赖运行方式。
    constexpr char kTempDir[]            = ID_BENCH_LOG_TMP_DIR;
    constexpr char kFileSinkName[]       = "idlog_bench_file.log";          // 截断/边界/非法级别/reconfigure 拒绝
    constexpr char kConcurrentName[]     = "idlog_bench_concurrent.log";    // 4 线程 × 100 条
    constexpr char kRotationStem[]       = "idlog_bench_rot";               // 轮转主文件 = stem + ext
    constexpr char kRotationExt[]        = ".log";
    constexpr char kRotationBaseName[]   = "idlog_bench_rot.log";
    constexpr char kConsoleCaptureName[] = "idlog_bench_console.log";       // console sink 捕获

    constexpr ID::uint64_t kRotationMaxBytes = 512;     // 单文件上限：远大于单行（约 75 字节）、远小于 40 行总量
    constexpr ID::uint32_t kRotationMaxFiles = 3;       // 保留 3 个轮转文件 → 文件集合 base/.1/.2/.3
    // 清理与断言都要覆盖 max_files + 1：spdlog 只保留 max_files 个轮转文件，
    // 若 .4 存在就说明命名或保留策略与本用例前提不符，必须能被发现而不是被忽略
    constexpr ID::uint32_t kRotationCleanupFiles = kRotationMaxFiles + 1;

    // 落盘内容里的唯一定位标记：逐条定位，而不是只数行数
    const char kNormalMessage[]       = "file sink normal line [FileCheck][Normal] ok";
    const char kAsciiTag[]            = "[FileCheck][Ascii] ";
    const char kUtf8Tag[]             = "[FileCheck][Utf8] ";
    const char kNormalMarker[]        = "[FileCheck][Normal]";
    const char kIllegalMessage[]      = "illegal";                              // write(999, "Bench", 4, "illegal", 7)
    const char kIllegalCategory[]     = "Bench";
    // 该行的 payload：category 只取前 4 字节，因此是 "Benc" 而不是 "Bench"
    const char kIllegalPayload[]      = "[Benc] illegal";
    const char kNullSegmentsPayload[] = "[] ";                                  // category/message 同时为 nullptr
    const char kZeroCategoryPayload[] = "[] zero category probe";               // category_len = 0
    const char kZeroMessagePayload[]  = "[Bench.Log.File] ";                    // message_len = 0
    const char kRejectProbeMarker[]   = "[RejectCheck] backend survives four rejected reconfigure";
    const char kConcurrentMarker[]    = "concurrent ";
    const char kRotationMarker[]      = "rotate line ";
    const char kConsoleMarker[]       = "[ConsoleCheck] console sink line";

    /**
     *  @brief 级别枚举转成服务表使用的数值
     *  @param level 公开头里的级别
     *  @return 对应的 uint32 数值
     */
    constexpr ID::uint32_t level_value(ID::Log::Level level) noexcept
    {
        return static_cast<ID::uint32_t>(level);
    }

    /**
     *  @brief 配置失败原因转成 `LogStats::last_config_error` 使用的数值
     *  @param error 公开头里的失败原因
     *  @return 对应的 uint32 数值
     */
    constexpr ID::uint32_t config_error(LogConfigError error) noexcept
    {
        return static_cast<ID::uint32_t>(error);
    }

    /**
     *  @brief 启动产品并取出 `ServiceSlot::Log` 上的服务表
     *  @param case_name start 或取表失败时要落盘的用例名
     *  @return 表非空时返回表；否则记录一条 Check 失败并返回 nullptr（调用方必须立即返回，不能解引用空表）
     *  @note `Engine::start()` 失败时运行期已经回滚到 Idle，这里不再补调 shutdown
     */
    const LogApiTable* start_product(const char* case_name) noexcept
    {
        const ID::PluginRuntimeDiagnostic started = ID::Engine::start();
        if (!started.is_ok())
        {
            ID::Bench::Check(false, case_name, "Engine::start() 必须成功；失败时运行期已回滚到 Idle");
            return nullptr;
        }

        const LogApiTable* const table =
            ID::PluginRuntime::instance().get_service<LogApiTable>(ID::ServiceSlot::Log);
        if (table == nullptr)
        {
            ID::Bench::Check(false, case_name, "start 进入 Ready 后 ServiceSlot::Log 必须已发布服务表");
        }
        return table;
    }

    /**
     *  @brief 关闭产品并记录一条结果
     *  @param case_name 用例名
     *  @return 关闭成功且运行期确实回到 Idle 时为 true
     */
    bool stop_product(const char* case_name) noexcept
    {
        const ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
        const bool ok = stopped.is_ok()
                     && ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Idle;
        ID::Bench::Check(ok, case_name, "关闭必须成功并回到 Idle");
        return ok;
    }

    /**
     *  @brief 读一次运行期诊断读数
     *  @param table 已发布的服务表
     *  @return 本次读数；后端不可用时 ready 为 false，其余字段被清零
     */
    LogStats read_stats(const LogApiTable* table) noexcept
    {
        LogStats stats{};
        table->get_stats(&stats);
        return stats;
    }

    /**
     *  @brief 检查一批句柄是否两两不同
     *  @param handles 句柄数组
     *  @param count 有效个数
     *  @return 没有任何两个句柄指向同一槽位时为 true
     *  @note 插件不做 category 查重（同一 category 重复申请得到不同槽位），因此槽位互不相同才算正确
     */
    bool handles_are_distinct(const Logger* const* handles, ID::uint32_t count) noexcept
    {
        for (ID::uint32_t i = 0; i < count; ++i)
        {
            for (ID::uint32_t j = i + 1; j < count; ++j)
            {
                if (handles[i] == handles[j])
                {
                    return false;
                }
            }
        }
        return true;
    }

    /**
     *  @brief 一次 `reconfigure` 之后可观测到的状态
     *  @note 全部字段取自 `get_stats`：失败时用于证明"旧后端与旧状态完全不变"
     */
    struct ReconfigureOutcome
    {
        bool         accepted       = false;
        bool         ready_after    = false;
        bool         async_after    = false;
        bool         console_after  = false;
        bool         file_after     = false;
        ID::uint32_t queue_capacity = 0;
        ID::uint32_t last_error     = 0;
    };

    /**
     *  @brief 调用一次 `reconfigure` 并立刻读取诊断读数
     *  @param table 已发布的服务表
     *  @param config 目标配置
     *  @return 返回值与结果状态
     */
    ReconfigureOutcome try_reconfigure(const LogApiTable* table, const LogConfig& config) noexcept
    {
        ReconfigureOutcome outcome{};
        outcome.accepted = table->reconfigure(config);

        const LogStats stats = read_stats(table);
        outcome.ready_after    = stats.ready;
        outcome.async_after    = stats.async;
        outcome.console_after  = stats.console;
        outcome.file_after     = stats.file;
        outcome.queue_capacity = stats.queue_capacity;
        outcome.last_error     = stats.last_config_error;
        return outcome;
    }

    /**
     *  @brief 判定一段字节是否是合法 UTF-8（从 begin 开始到结尾）
     *  @param text 待判定内容
     *  @param begin 起始下标
     *  @return 每个码点都完整且合法时为 true
     *  @note 只做结构校验（首字节类别 + 延续字节数量），足以识别"截断留下半个码点"
     */
    bool is_valid_utf8(const std::string& text, ID::size_t begin) noexcept
    {
        ID::size_t index = begin;
        while (index < text.size())
        {
            const auto lead = static_cast<unsigned char>(text[index]);
            if (lead < 0x80)
            {
                ++index;
                continue;
            }

            ID::size_t extra = 0;
            if ((lead & 0xE0) == 0xC0)
            {
                extra = 1;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                extra = 2;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                extra = 3;
            }
            else
            {
                return false;   // 孤立的延续字节或非法首字节
            }

            if (index + extra >= text.size())
            {
                return false;   // 尾部留下不完整的多字节序列
            }
            for (ID::size_t offset = 1; offset <= extra; ++offset)
            {
                if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80)
                {
                    return false;
                }
            }
            index += extra + 1;
        }
        return true;
    }

    using LineList = std::vector<std::string>;

    /**
     *  @brief 取当前进程号，用于把临时日志文件名变成进程唯一
     *  @return 当前进程号；同一台机器上并存的两个进程必然不同
     *  @note 只在拼路径的冷路径调用，不进任何测量循环
     */
    ID::uint32_t current_process_id() noexcept
    {
#if defined(_WIN32)
        return static_cast<ID::uint32_t>(::_getpid());
#else
        return static_cast<ID::uint32_t>(::getpid());
#endif
    }

    /**
     *  @brief 在扩展名前插入进程唯一后缀：`stem.ext` → `stem_p<pid>.ext`
     *  @param out 目标缓冲
     *  @param capacity 目标缓冲字节数
     *  @param name 原始文件名（允许不带扩展名）
     *  @note 后缀必须插在最后一个 '.' 之前：spdlog rotating sink 以该位置切分 stem/ext，
     *        主文件 `idlog_bench_rot_p123.log` 对应的轮转名才是 `idlog_bench_rot_p123.1.log`
     */
    void unique_temp_name(char* out, std::size_t capacity, const char* name) noexcept
    {
        const char* const dot = std::strrchr(name, '.');
        const unsigned    pid = static_cast<unsigned>(current_process_id());
        if (dot == nullptr)
        {
            std::snprintf(out, capacity, "%s_p%u", name, pid);
            return;
        }

        const int stem_bytes = static_cast<int>(dot - name);
        std::snprintf(out, capacity, "%.*s_p%u%s", stem_bytes, name, pid, dot);
    }

    /**
     *  @brief 把临时文件名拼到 `ID_BENCH_LOG_TMP_DIR` 下（文件名带当前进程号）
     *  @param out 目标缓冲，容量必须是 `kMaxFilePathBytes`（服务表的 file_path 容量）
     *  @param capacity 目标缓冲字节数
     *  @param name 文件名
     *  @note 拼出来的路径长度必须小于 `kMaxFilePathBytes`，否则 reconfigure 的 file_path 会被截断
     *  @note 固定文件名会让同一构建目录里的两个进程互相截断/删除对方的落盘文件
     *        （实测并发跑 4×100 行只剩 20 行），因此每个进程独占一组带进程号的临时文件
     */
    void temp_path(char* out, std::size_t capacity, const char* name) noexcept
    {
        char unique[kMaxFilePathBytes] = {};
        unique_temp_name(unique, sizeof(unique), name);
        std::snprintf(out, capacity, "%s/%s", kTempDir, unique);
    }

    /**
     *  @brief 拼出轮转后的文件名：spdlog rotating sink 用 `<stem>.<index><ext>`
     *  @param out 目标缓冲，容量必须是 `kMaxFilePathBytes`
     *  @param capacity 目标缓冲字节数
     *  @param index 轮转序号，从 1 开始
     *  @note stem 也必须带同一个进程号，否则断言会去查别的进程的轮转文件
     */
    void rotated_path(char* out, std::size_t capacity, ID::uint32_t index) noexcept
    {
        char stem[kMaxFilePathBytes] = {};
        unique_temp_name(stem, sizeof(stem), kRotationStem);
        std::snprintf(out, capacity, "%s/%s.%u%s", kTempDir, stem, index, kRotationExt);
    }

    /**
     *  @brief 读一个文件的大小
     *  @param path 文件路径
     *  @return 文件存在时返回字节数；不存在或打不开返回 -1
     */
    long long file_size(const char* path) noexcept
    {
        std::FILE* const file = std::fopen(path, "rb");
        if (file == nullptr)
        {
            return -1;
        }

        long long size = -1;
        if (std::fseek(file, 0, SEEK_END) == 0)
        {
            const long end = std::ftell(file);
            if (end >= 0)
            {
                size = static_cast<long long>(end);
            }
        }
        std::fclose(file);
        return size;
    }

    /**
     *  @brief 删除轮转用例的主文件与全部轮转文件（含多出一级的 .4）
     *  @return 清理后这些路径都不存在时为 true
     *  @note 轮转文件名由 spdlog 决定（`<stem>.<index><ext>`，index 从 1 起），因此删除是
     *        "先按已知集合删、再逐个确认不存在"，可重复执行；集合取 0..max_files+1，
     *        多出来的 .4 既删掉也要求不存在，避免残留掩盖命名前提的变化
     */
    bool remove_rotation_set() noexcept
    {
        char path[kMaxFilePathBytes] = {};
        temp_path(path, sizeof(path), kRotationBaseName);
        std::remove(path);

        for (ID::uint32_t index = 1; index <= kRotationCleanupFiles; ++index)
        {
            rotated_path(path, sizeof(path), index);
            std::remove(path);
        }

        temp_path(path, sizeof(path), kRotationBaseName);
        bool all_gone = file_size(path) < 0;
        for (ID::uint32_t index = 1; index <= kRotationCleanupFiles; ++index)
        {
            rotated_path(path, sizeof(path), index);
            all_gone = all_gone && file_size(path) < 0;
        }
        return all_gone;
    }

    /**
     *  @brief 读取一个文本文件并按 '\n' 切行
     *  @param path 文件路径
     *  @param lines 输出：不含行尾 '\n' / '\r' 的行
     *  @return 文件能打开时为 true（空文件算成功，lines 为空）
     *  @note 用 C 文件 API 读取：`std::ifstream`/`std::getline` 在 MSVC 的 `/EHs-c-`（关闭展开语义）下
     *        会触发 C4530（STL 内部用了 try/catch），而本工程不使用异常
     */
    bool read_lines(const char* path, LineList* lines) noexcept
    {
        std::FILE* const file = std::fopen(path, "rb");
        if (file == nullptr)
        {
            return false;
        }

        std::string content;
        char chunk[512];
        for (;;)
        {
            const std::size_t read_bytes = std::fread(chunk, 1, sizeof(chunk), file);
            if (read_bytes == 0)
            {
                break;
            }
            content.append(chunk, read_bytes);
        }
        std::fclose(file);

        std::string::size_type line_begin = 0;
        while (line_begin < content.size())
        {
            const std::string::size_type newline   = content.find('\n', line_begin);
            const std::string::size_type line_end  = (newline == std::string::npos) ? content.size() : newline;

            std::string line = content.substr(line_begin, line_end - line_begin);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines->push_back(std::move(line));

            if (newline == std::string::npos)
            {
                break;
            }
            line_begin = newline + 1;
        }
        return true;
    }

    /**
     *  @brief 统计含指定子串的行数
     */
    ID::size_t count_containing(const LineList& lines, const std::string& needle) noexcept
    {
        ID::size_t count = 0;
        for (const std::string& line : lines)
        {
            if (line.find(needle) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    }

    /**
     *  @brief 统计以指定子串结尾的行数
     */
    ID::size_t count_ending_with(const LineList& lines, const std::string& suffix) noexcept
    {
        ID::size_t count = 0;
        for (const std::string& line : lines)
        {
            if (line.size() >= suffix.size()
                && line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                ++count;
            }
        }
        return count;
    }

    /**
     *  @brief 找到第一条含指定子串的行
     *  @return 命中行的指针；没有命中返回 nullptr
     */
    const std::string* find_line(const LineList& lines, const std::string& needle) noexcept
    {
        for (const std::string& line : lines)
        {
            if (line.find(needle) != std::string::npos)
            {
                return &line;
            }
        }
        return nullptr;
    }

    /**
     *  @brief 判断两份诊断读数是否逐字段相同
     *  @note 不比较对象字节：`LogStats` 有填充字节，memcmp 会把未初始化填充当成差异
     */
    bool stats_equal(const LogStats& left, const LogStats& right) noexcept
    {
        return left.overrun_dropped        == right.overrun_dropped
            && left.discarded              == right.discarded
            && left.dropped_before_ready   == right.dropped_before_ready
            && left.dropped_after_shutdown == right.dropped_after_shutdown
            && left.queue_capacity         == right.queue_capacity
            && left.active_level           == right.active_level
            && left.last_config_error      == right.last_config_error
            && left.ready                  == right.ready
            && left.async                  == right.async
            && left.console                == right.console
            && left.file                   == right.file;
    }

    // ---- 进程内 stderr 重定向（只用于 console sink 的一次落盘读取）----
    // 平台差异用显式分支：MSVC/MinGW 提供 `_dup/_dup2/_fileno/_close`，POSIX 提供不带下划线的同名函数。

    int duplicate_fd(int fd) noexcept
    {
        #if defined(_WIN32)
            return ::_dup(fd);
        #else
            return ::dup(fd);
        #endif
    }

    int file_descriptor(std::FILE* file) noexcept
    {
        #if defined(_WIN32)
            return ::_fileno(file);
        #else
            return ::fileno(file);
        #endif
    }

    int redirect_fd(int target, int source) noexcept
    {
        #if defined(_WIN32)
            return ::_dup2(source, target);
        #else
            return ::dup2(source, target);
        #endif
    }

    void close_descriptor(int fd) noexcept
    {
        if (fd < 0)
        {
            return;
        }
        #if defined(_WIN32)
            ::_close(fd);
        #else
            ::close(fd);
        #endif
    }

} // namespace

namespace ID::Bench
{
    void IDLogSecurityTest() noexcept
    {
        // =================================================================
        // 轮 1：ABI 契约 / 运行期诊断 / configure / reconfigure 拓扑重建
        // =================================================================
        const LogApiTable* const table = start_product("log.engine.start");
        if (table == nullptr)
        {
            return;     // 已经记录失败：不继续解引用空表，也不 crash
        }

        // 判据必须来自 DUT 发布的表，而不是本编译单元里的常量：后者只能证明头文件同版本
        Check(table->revision == 2u, "log.api.revision_value",
              "DUT 发布的 LogApiTable::revision 必须是 2（rev2 六入口服务表）");
        static_assert(kLogApiRevision == 2u,
                      "rev2 服务表修订号必须是 2（补充断言，不能代替上面对 DUT 的检查）");
        Check(table->write != nullptr && table->configure != nullptr && table->flush != nullptr
           && table->acquire_logger != nullptr && table->reconfigure != nullptr && table->get_stats != nullptr
           && table->active_level != nullptr && table->revision == kLogApiRevision,
              "log.api.table_complete",
              "六个函数指针与 active_level 都不得为空，revision 必须等于 kLogApiRevision(2)");
        Check(ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Ready,
              "log.engine.state_ready",
              "start 成功后运行期必须是 Ready");

        {
            // 产品入口的状态机：Ready 重复 start 必须被拒且不改变流程
            const ID::PluginRuntimeDiagnostic second = ID::Engine::start();
            Check(second.code == ID::PluginRuntimeError::AlreadyStarted
               && ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Ready,
                  "log.engine.start_twice",
                  "Ready 重复 start 必须返回 AlreadyStarted 且保持 Ready");
        }

        // ---------- C1：get_stats 的 Ready 读数（空队列） ----------
        {
            LogStats stats{};
            table->get_stats(&stats);
            Check(stats.ready, "log.stats.ready", "Ready 区间内 get_stats 必须报告 ready=true");
            Check(stats.async && stats.console && !stats.file,
                  "log.stats.topology",
                  "on_init 默认配置是 console + async、无文件 sink");
            Check(stats.queue_capacity == 8192u, "log.stats.queue_capacity",
                  "默认 queue_size=8192，异步形态下必须如实回报");
            Check(stats.active_level == level_value(ID::Log::Level::Info), "log.stats.active_level",
                  "默认 min_level=Info，读数必须与 active_level 原子量同源");
            Check(stats.last_config_error == 0u, "log.stats.last_config_error",
                  "成功 init 之后最近一次配置错误必须是 None(0)");
            Check(stats.overrun_dropped == 0u && stats.discarded == 0u, "log.stats.empty_queue_counters",
                  "尚无写入时队列溢出与显式丢弃计数必须为 0");

            // nullptr 目标的契约是"什么都不做"：前后各取一次真实读数，必须逐字段一致
            LogStats after_null{};
            table->get_stats(nullptr);
            table->get_stats(&after_null);
            Check(stats_equal(stats, after_null), "log.stats.null_target_no_effect",
                  "get_stats(nullptr) 不得写任何状态：前后两次真实读数必须逐字段相同");
        }

        // ---------- C1：acquire_logger 的正常与非法输入 ----------
        Logger* const logger = ID::Log::acquire_logger(*table, kCategory);
        Check(logger != nullptr, "log.logger.acquire", "正常 category 必须拿到句柄");
        if (logger != nullptr)
        {
            Check(logger->table == table
               && logger->category == kCategory
               && logger->category_len == kCategoryBytes,
                  "log.logger.handle_fields",
                  "句柄必须原样携带表指针、category 指针与字节长度（插件只保存指针、不复制）");
        }

        Check(table->acquire_logger(nullptr, 4) == nullptr,
              "log.logger.null_category",
              "category 为 nullptr 时必须返回 nullptr");

        {
            const Logger* const empty_category = ID::Log::acquire_logger(*table, "");
            Check(empty_category != nullptr && empty_category->category_len == 0,
                  "log.logger.empty_category",
                  "空字面量指针非空：category_len=0 仍应拿到句柄");
        }

        // ---------- C1/C3：write 的越界 level 必须不 abort 且不改过滤级别 ----------
        // 收敛目标（是否恰为 Error）与 nullptr/零长段的落盘内容都在轮 1b 用文件读回断言，
        // 这里只验证异步路径上"不 abort、不改过滤真源"这两条不可替代的行为。
        {
            const char kIllegalProbe[] = "illegal level probe";
            const ID::uint32_t level_before = table->active_level->load(std::memory_order_relaxed);

            // 契约：越界整数必须收敛为 Error，绝不允许把进程推进 Fatal 的 abort 分支
            table->write(999u, kCategory, kCategoryBytes,
                         kIllegalProbe, static_cast<ID::uint32_t>(sizeof(kIllegalProbe) - 1));
            table->write(0xFFFFFFFFu, kCategory, kCategoryBytes,
                         kIllegalProbe, static_cast<ID::uint32_t>(sizeof(kIllegalProbe) - 1));

            Check(table->active_level->load(std::memory_order_relaxed) == level_before,
                  "log.write.illegal_level_no_abort",
                  "level=999/0xFFFFFFFF 不得触发 Fatal 的 abort，也不得改变过滤级别");
        }

        // ---------- C2/C6：超长 message 的源缓冲哨兵 ----------
        {
            constexpr ID::uint32_t kGuard      = 64;
            constexpr ID::uint32_t kMessageLen = 4096;      // 远超 kLineCapacity(1024) 与 spdlog 250 内联容量
            char payload[kGuard + kMessageLen + kGuard];

            std::memset(payload, 'A', sizeof(payload));
            std::memset(payload, 0x5A, kGuard);                                  // 前哨兵
            std::memset(payload + kGuard + kMessageLen, 0xA5, kGuard);           // 后哨兵

            table->write(level_value(ID::Log::Level::Info), kCategory, kCategoryBytes,
                         payload + kGuard, kMessageLen);

            bool guard_intact = true;
            for (ID::uint32_t i = 0; i < kGuard; ++i)
            {
                guard_intact = guard_intact
                            && static_cast<unsigned char>(payload[i]) == 0x5A
                            && static_cast<unsigned char>(payload[kGuard + kMessageLen + i]) == 0xA5;
            }
            // 本条的判据只有"调用方缓冲区前后哨兵未被改写"；截断本身在这条异步 console 路径上
            // 不可观测，因此不在 detail 里宣称，改由落盘用例 log.file.sink.ascii_1024 读回证明
            Check(guard_intact, "log.write.oversize_message_guard",
                  "4KB message 不得越界读写调用方缓冲区（前后 64B 哨兵必须原样）；"
                  "截断结果本身由 log.file.sink.ascii_1024 的读回断言覆盖，本条不断言");
        }

        // ---------- C1/C4：configure 的合法项、越界项与拓扑项 ----------
        {
            LogConfig same_as_default{};
            Check(table->configure(same_as_default),
                  "log.configure.idempotent",
                  "与 on_init 默认配置一致的 configure 必须返回 true（只改允许项不需要重启）");
        }

        {
            LogConfig quiet{};
            quiet.min_level = level_value(ID::Log::Level::Error);
            Check(table->configure(quiet), "log.configure.min_level",
                  "合法 min_level 变更必须返回 true");
            Check(table->active_level->load(std::memory_order_relaxed) == quiet.min_level,
                  "log.configure.min_level_applied",
                  "active_level 是唯一过滤真源，configure 之后必须立即生效");

            LogConfig out_of_range = quiet;
            out_of_range.min_level = level_value(ID::Log::Level::Fatal) + 1;    // 6 = 越界
            Check(!table->configure(out_of_range), "log.configure.min_level_out_of_range",
                  "min_level 越界（6）必须返回 false");
            Check(read_stats(table).last_config_error == config_error(LogConfigError::InvalidLevel),
                  "log.configure.min_level_error",
                  "越界级别必须把 last_config_error 记成 InvalidLevel");
            Check(table->active_level->load(std::memory_order_relaxed) == quiet.min_level,
                  "log.configure.reject_keeps_level",
                  "被拒的 configure 不得改变级别真源");
        }

        // ---------- C1/C4：拓扑差异必须被 configure 拒绝并要求 reconfigure ----------
        {
            const ID::uint32_t level_before = table->active_level->load(std::memory_order_relaxed);

            LogConfig topology{};
            topology.min_level = level_value(ID::Log::Level::Trace);            // 同时改级别，验证"整条被拒"
            topology.enable_console = false;

            const bool accepted = table->configure(topology);
            const LogStats stats = read_stats(table);

            Check(!accepted, "log.configure.topology_rejected",
                  "改 enable_console 属于拓扑项，Ready 区间必须返回 false");
            Check(stats.last_config_error == config_error(LogConfigError::TopologyRequiresReconfigure),
                  "log.configure.topology_error",
                  "拓扑差异必须把 last_config_error 记成 TopologyRequiresReconfigure");
            Check(table->active_level->load(std::memory_order_relaxed) == level_before,
                  "log.configure.topology_keeps_level",
                  "拓扑字段被拒时不得顺手应用同一请求里的 min_level");

            LogConfig restored{};
            Check(table->configure(restored), "log.configure.restore",
                  "把 min_level 恢复成 Info 必须成功，且 last_config_error 回到 None");
            Check(read_stats(table).last_config_error == 0u, "log.configure.restore_error_cleared",
                  "成功的 configure 必须把 last_config_error 清回 None(0)");
        }

        // ---------- C3：ABI 握手字段不符必须被拒 ----------
        {
            LogConfig bad_size{};
            bad_size.struct_size = 1;                                          // 与本构建 sizeof(LogConfig) 不符
            Check(!table->configure(bad_size), "log.configure.envelope_struct_size",
                  "struct_size=1 与当前构建不匹配，configure 必须返回 false");
            Check(read_stats(table).last_config_error == config_error(LogConfigError::InvalidEnvelope),
                  "log.configure.envelope_struct_size_error",
                  "握手失败必须把 last_config_error 记成 InvalidEnvelope");

            LogConfig bad_revision{};
            bad_revision.config_revision = 99;                                 // 与 kLogApiRevision 不符
            Check(!table->configure(bad_revision), "log.configure.envelope_revision",
                  "config_revision=99 与当前构建不匹配，configure 必须返回 false");
            Check(read_stats(table).last_config_error == config_error(LogConfigError::InvalidEnvelope),
                  "log.configure.envelope_revision_error",
                  "握手失败必须把 last_config_error 记成 InvalidEnvelope");

            LogConfig restored{};
            Check(table->configure(restored), "log.configure.envelope_restore",
                  "恢复合法配置必须成功（被拒的握手请求不改变任何状态）");
        }

        // ---------- C4：reconfigure 异步 → 同步 → 异步 ----------
        {
            LogConfig sync_config{};
            sync_config.async      = false;
            sync_config.queue_size = 0;                                        // 同步形态不校验队列容量

            const ReconfigureOutcome to_sync = try_reconfigure(table, sync_config);
            Check(to_sync.accepted, "log.reconfigure.to_sync",
                  "异步→同步是拓扑变更：reconfigure 必须接受 async=false + queue_size=0");
            Check(to_sync.async_after == false && to_sync.queue_capacity == 0u,
                  "log.reconfigure.to_sync_stats",
                  "同步形态下 stats.async=false 且 queue_capacity=0（不进队列）");
            Check(to_sync.ready_after && to_sync.console_after && !to_sync.file_after,
                  "log.reconfigure.to_sync_topology",
                  "同步后仍是 console sink、无文件 sink，后端处于可用状态");

            // 同步形态"写出去"的证据不在这里：console 由 log.output.console_content 读回，
            // 文件由轮 1b/1c/1d 读回，避免留下"调用返回即通过"的恒真断言
            LogConfig async_config{};
            const ReconfigureOutcome to_async = try_reconfigure(table, async_config);
            Check(to_async.accepted && to_async.async_after && to_async.queue_capacity == 8192u,
                  "log.reconfigure.to_async",
                  "同步→异步（queue_size=8192）必须成功，stats 回到 async 且容量如实回报");
        }

        // ---------- C4：只有运行期可调项变化时，reconfigure 等价于 configure ----------
        {
            LogConfig level_only{};
            level_only.min_level = level_value(ID::Log::Level::Warn);

            const ReconfigureOutcome outcome = try_reconfigure(table, level_only);
            Check(outcome.accepted, "log.reconfigure.level_only",
                  "拓扑一致的 reconfigure 必须成功（等价 configure，不重建后端）");
            Check(table->active_level->load(std::memory_order_relaxed) == level_only.min_level,
                  "log.reconfigure.level_only_applied",
                  "级别必须立刻生效");
            Check(outcome.queue_capacity == 8192u && outcome.async_after,
                  "log.reconfigure.level_only_keeps_topology",
                  "只改级别不得改变队列容量与同步/异步形态");

            // 这里走 configure（只改级别），名字必须是 configure 而不是 reconfigure：
            // 上一条 reconfigure 已经证明"拓扑一致时等价 configure"，本条只验证用 configure 也能恢复级别
            LogConfig restored{};
            Check(table->configure(restored), "log.configure.restore_after_reconfig",
                  "reconfigure 之后用 configure 把 min_level 恢复成 Info 必须成功");
        }

        // =================================================================
        // 轮 1b：console sink 的真实输出（进程内临时重定向 stderr 后读回）
        // =================================================================
        {
            char capture_path[kMaxFilePathBytes] = {};
            temp_path(capture_path, sizeof(capture_path), kConsoleCaptureName);
            // 捕获路径同样受服务表 file_path 容量约束：先确认没被 snprintf 截断，再拿它做重定向目标
            const bool capture_path_ok = std::strlen(capture_path) < kMaxFilePathBytes - 1;
            std::remove(capture_path);

            std::FILE* const capture          = std::fopen(capture_path, "wb");
            const int        saved_fd         = (capture != nullptr) ? duplicate_fd(2) : -1;
            bool             redirected       = false;
            if (capture != nullptr && saved_fd >= 0)
            {
                redirected = redirect_fd(2, file_descriptor(capture)) == 0;
            }

            // 先重定向、再 reconfigure：console sink 在构造时缓存 `_get_osfhandle(_fileno(stderr))`，
            // 先建 sink 再重定向对 Windows 无效（那会让断言变成假通过）
            LogConfig console_config{};
            console_config.min_level      = level_value(ID::Log::Level::Trace);
            console_config.enable_console = true;
            console_config.enable_file    = false;
            console_config.async          = false;
            console_config.queue_size     = 0;

            const ReconfigureOutcome to_console = try_reconfigure(table, console_config);
            Check(to_console.accepted && to_console.console_after && !to_console.file_after && !to_console.async_after,
                  "log.console.reconfigure",
                  "同步 console 形态必须被 reconfigure 接受（enable_console=true、无文件 sink）");

            table->write(level_value(ID::Log::Level::Info), kCategory, kCategoryBytes,
                         kConsoleMarker, static_cast<ID::uint32_t>(sizeof(kConsoleMarker) - 1));
            table->flush();     // 同步形态：返回时内容已交给 sink

            // 恢复 stderr：`_dup2` 的返回值必须检查；恢复是否真的生效用"向 stderr 写探针后捕获文件不变"证明。
            // 不能比较句柄数值：Windows 上 `_dup2` 会重新复制一个句柄，同一目标的句柄值本来就会变
            // （MSVC 实测 0x954 → 0x1FC），按同值判定会得到假失败
            bool restore_called = false;
            if (redirected)
            {
                restore_called = redirect_fd(2, saved_fd) == 0;
            }

            long long capture_before = -1;
            long long capture_after  = -1;
            if (capture != nullptr)
            {
                std::fflush(capture);
                capture_before = file_size(capture_path);

                const char kRestoreProbe[] = "[ConsoleCheck] stderr restore probe\n";
                static_cast<void>(std::fwrite(kRestoreProbe, 1, sizeof(kRestoreProbe) - 1, stderr));
                std::fflush(stderr);
                std::fflush(capture);

                capture_after = file_size(capture_path);
            }

            char restore_detail[360];
            std::snprintf(restore_detail, sizeof(restore_detail),
                          "重定向%s；_dup2 恢复%s；恢复后向 stderr 写探针，捕获文件 %lld → %lld 字节（必须不变）",
                          redirected ? "成功" : "失败",
                          restore_called ? "返回 0" : "失败或未执行",
                          capture_before, capture_after);
            Check(restore_called && capture_before >= 0 && capture_after == capture_before,
                  "log.console.stderr_restored", restore_detail);

            LogConfig console_async{};
            const ReconfigureOutcome console_restored = try_reconfigure(table, console_async);
            Check(console_restored.accepted && console_restored.console_after && console_restored.async_after
               && !console_restored.file_after,
                  "log.console.reconfigure_restore",
                  "回到 console + async 默认形态必须成功：accepted=true、console=true、async=true、file=false");
            close_descriptor(saved_fd);
            if (capture != nullptr)
            {
                std::fclose(capture);
            }

            LineList          lines;
            const bool        opened    = read_lines(capture_path, &lines);
            const std::string expected  = std::string("[") + kCategory + "] " + kConsoleMarker;
            const ID::size_t  hits      = count_ending_with(lines, expected);
            const ID::size_t  info_hits = count_containing(lines, "[info]");
            char detail[560];
            std::snprintf(detail, sizeof(detail),
                          "捕获路径 %s（%s kMaxFilePathBytes=%u）；stderr 重定向%s；捕获 %zu 行，"
                          "含 [info] 的行 %zu、以 \"%s\" 结尾的行 %zu（期望各 1）",
                          capture_path, capture_path_ok ? "未超" : "超出", static_cast<unsigned>(kMaxFilePathBytes),
                          redirected ? "成功" : "失败", lines.size(), info_hits, expected.c_str(), hits);
            Check(capture_path_ok && redirected && opened && hits == 1u && info_hits == 1u,
                  "log.output.console_content", detail);
            Check(std::remove(capture_path) == 0, "log.console.cleanup",
                  "用例结束必须能删除 console 捕获文件");
        }

        // =================================================================
        // 轮 1c：文件 sink（同步形态）+ 截断 / 边界 / 非法级别 / 拒绝矩阵的真实落盘核对
        // =================================================================
        {
            char file_path[kMaxFilePathBytes] = {};
            temp_path(file_path, sizeof(file_path), kFileSinkName);
            const bool path_ok = std::strlen(file_path) < kMaxFilePathBytes - 1;
            std::remove(file_path);   // 清掉上一轮可能残留的文件，保证读到的是本轮写入

            LogConfig file_config{};
            file_config.min_level       = level_value(ID::Log::Level::Trace);
            file_config.enable_console  = false;
            file_config.enable_file     = true;
            file_config.async           = false;
            file_config.queue_size      = 0;
            std::snprintf(file_config.file_path, sizeof(file_config.file_path), "%s", file_path);

            const ReconfigureOutcome to_file = try_reconfigure(table, file_config);
            Check(path_ok && to_file.accepted, "log.reconfigure.file_sync",
                  "console→文件 sink + 同步形态必须被 reconfigure 接受（路径来自 ID_BENCH_LOG_TMP_DIR）");
            Check(to_file.ready_after && to_file.file_after && !to_file.console_after && !to_file.async_after,
                  "log.reconfigure.file_sync_stats",
                  "文件 sink 生效后 stats 必须报告 file=true / console=false / async=false");
            Check(read_stats(table).active_level == level_value(ID::Log::Level::Trace),
                  "log.reconfigure.file_sync_level",
                  "reconfigure 必须同时应用请求里的 min_level=Trace（拓扑重建不吞掉级别）");

            // ---- 第 1 行：普通行（走调用侧入口，格式化在调用方完成，栈缓冲 512）----
            if (logger != nullptr)
            {
                logger->info("{}", kNormalMessage);
            }
            else
            {
                table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes,
                             kNormalMessage, static_cast<ID::uint32_t>(sizeof(kNormalMessage) - 1));
            }

            // ---- 第 2 行：4KB ASCII，截断点正好落在 capacity-3，payload 必须恰好 1024 字节且以 "..." 结尾 ----
            char ascii_message[4096];
            std::memset(ascii_message, 'C', sizeof(ascii_message));
            std::memcpy(ascii_message, kAsciiTag, sizeof(kAsciiTag) - 1);

            // ---- 第 3 行：4KB UTF-8，3 字节码点（'数' = E6 95 B0）重复，截断点必然落在多字节序列中间 ----
            char utf8_message[4096];
            std::memcpy(utf8_message, kUtf8Tag, sizeof(kUtf8Tag) - 1);
            ID::size_t utf8_len = sizeof(kUtf8Tag) - 1;
            while (utf8_len + 3 <= sizeof(utf8_message))
            {
                utf8_message[utf8_len]      = static_cast<char>(0xE6);
                utf8_message[utf8_len + 1]  = static_cast<char>(0x95);
                utf8_message[utf8_len + 2]  = static_cast<char>(0xB0);
                utf8_len += 3;
            }

            // 第 1~3 行必须用**同一级别**写出：spdlog pattern 的 %l 字段宽度随级别名变化
            // （error 比 info 多一个字符），只有同一级别下 pattern 前缀才等长，前缀才能从普通行反推。
            // min_level=Trace 时 Info 一律通过，级别过滤本身由 configure 用例覆盖。
            table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes,
                         ascii_message, static_cast<ID::uint32_t>(sizeof(ascii_message)));
            table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes,
                         utf8_message, static_cast<ID::uint32_t>(utf8_len));

            // ---- 第 4 行：越界 level 必须收敛为 Error（落盘行的 %l 是 error），且进程不 abort ----
            // category 只传前 4 字节（"Benc"）：同时覆盖"按长度而不是按 NUL 读取 category"这条契约
            table->write(999u, kIllegalCategory, 4u, kIllegalMessage,
                         static_cast<ID::uint32_t>(sizeof(kIllegalMessage) - 1));

            // ---- 第 5~7 行：nullptr 段与零长段不写任何字节 ----
            table->write(level_value(ID::Log::Level::Info), nullptr, 0u, nullptr, 0u);
            const char kZeroCategoryProbe[] = "zero category probe";
            table->write(level_value(ID::Log::Level::Info), "", 0u, kZeroCategoryProbe,
                         static_cast<ID::uint32_t>(sizeof(kZeroCategoryProbe) - 1));
            table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes, "", 0u);

            // ---- 第 8~9 行：payload 恰好 1024 字节（不截断）与多请求 1 字节（截断到 1024 并补 "..."）----
            constexpr ID::uint32_t kFilePrefixBytes = 1u + kFileCategoryBytes + 2u;     // "[category] "
            constexpr ID::uint32_t kFitMessageBytes = kLineCapacity - kFilePrefixBytes;
            static_assert(kFilePrefixBytes + kFitMessageBytes == kLineCapacity,
                          "边界消息长度必须由 kLineCapacity 推出，不能写死");
            char fit_message[kFitMessageBytes];
            std::memset(fit_message, 'D', sizeof(fit_message));
            char over_message[kFitMessageBytes + 1u];
            std::memset(over_message, 'E', sizeof(over_message));
            table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes,
                         fit_message, kFitMessageBytes);
            table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes,
                         over_message, kFitMessageBytes + 1u);

            // ---- reconfigure 失败矩阵：旧后端（file + sync）与旧状态必须完全不变 ----
            {
                LogConfig bad_queue{};
                bad_queue.queue_size = 3;                                      // 异步形态必须是 2 的幂
                const ReconfigureOutcome outcome = try_reconfigure(table, bad_queue);
                Check(!outcome.accepted, "log.reconfigure.reject_queue_size",
                      "async 形态下 queue_size=3 不是 2 的幂，reconfigure 必须返回 false");
                Check(outcome.last_error == config_error(LogConfigError::InvalidQueueSize),
                      "log.reconfigure.reject_queue_size_error",
                      "失败原因必须是 InvalidQueueSize");
                Check(outcome.ready_after && outcome.file_after && !outcome.console_after
                   && !outcome.async_after && outcome.queue_capacity == 0u,
                      "log.reconfigure.reject_queue_size_keeps_backend",
                      "失败后旧后端必须仍可用：仍是 file+sync（console=false、queue_capacity=0）");
            }

            {
                LogConfig missing_path{};
                missing_path.enable_file = true;                               // file_path 仍是空串
                const ReconfigureOutcome outcome = try_reconfigure(table, missing_path);
                Check(!outcome.accepted, "log.reconfigure.reject_missing_path",
                      "enable_file=true 但 file_path 为空，reconfigure 必须返回 false");
                Check(outcome.last_error == config_error(LogConfigError::MissingFilePath),
                      "log.reconfigure.reject_missing_path_error",
                      "失败原因必须是 MissingFilePath");
                Check(outcome.ready_after && outcome.file_after && !outcome.console_after,
                      "log.reconfigure.reject_missing_path_keeps_backend",
                      "失败后旧后端必须仍可用：仍是 file sink、console=false");
            }

            {
                LogConfig directory_path{};
                directory_path.enable_file = true;
                std::snprintf(directory_path.file_path, sizeof(directory_path.file_path), "%s", ".");
                const ReconfigureOutcome outcome = try_reconfigure(table, directory_path);
                Check(!outcome.accepted, "log.reconfigure.reject_not_writable",
                      "enable_file 指向目录（'.'）不可写，reconfigure 必须返回 false");
                Check(outcome.last_error == config_error(LogConfigError::FileNotWritable),
                      "log.reconfigure.reject_not_writable_error",
                      "失败原因必须是 FileNotWritable（建后端之前先探测）");
                Check(outcome.ready_after && outcome.file_after && !outcome.console_after,
                      "log.reconfigure.reject_not_writable_keeps_backend",
                      "失败后旧后端必须仍可用：仍是 file sink、console=false");
            }

            {
                LogConfig no_sink{};
                no_sink.enable_console = false;
                no_sink.enable_file    = false;
                const ReconfigureOutcome outcome = try_reconfigure(table, no_sink);
                Check(!outcome.accepted, "log.reconfigure.reject_no_sink",
                      "console 与 file 都关闭时日志无处可去，reconfigure 必须返回 false");
                Check(outcome.last_error == config_error(LogConfigError::NoSink),
                      "log.reconfigure.reject_no_sink_error",
                      "失败原因必须是 NoSink");
                Check(outcome.ready_after && outcome.file_after && !outcome.console_after,
                      "log.reconfigure.reject_no_sink_keeps_backend",
                      "失败后旧后端必须仍可用：仍是 file sink、console=false");
            }

            // ---- 第 10 行：四次被拒之后旧后端必须真的还能写出内容（读回文件才算证据）----
            table->write(level_value(ID::Log::Level::Info), kCategory, kCategoryBytes,
                         kRejectProbeMarker, static_cast<ID::uint32_t>(sizeof(kRejectProbeMarker) - 1));
            table->flush();     // 同步形态：返回时内容已交给 OS

            // 回到 console + async：reconfigure 会排空并销毁旧后端，文件 sink 在此关闭
            LogConfig console_config{};
            const ReconfigureOutcome to_console = try_reconfigure(table, console_config);
            Check(to_console.accepted && to_console.console_after && !to_console.file_after && to_console.async_after,
                  "log.reconfigure.file_closed",
                  "回到 console + async 必须成功：旧后端被排空销毁、文件 sink 关闭");

            LineList   lines;
            const bool opened = read_lines(file_path, &lines);
            char       detail[400];
            std::snprintf(detail, sizeof(detail), "文件 %s 应恰好 10 行，实际 %zu 行", file_path, lines.size());
            Check(opened && lines.size() == 10u, "log.file.sink.line_count", detail);
            if (opened)
            {
                // 普通行经 logger 句柄写出，category 是句柄自己的 "Bench.Log"（不是文件用例的
                // "Bench.Log.File"）：payload 必须按句柄的 category 计算，否则前缀反推会错位
                const std::string normal_payload = std::string("[") + kCategory + "] " + kNormalMessage;
                const std::string* const normal_line = find_line(lines, kNormalMarker);
                const bool normal_ok = normal_line != nullptr
                                    && normal_line->size() > normal_payload.size()
                                    && normal_line->compare(normal_line->size() - normal_payload.size(),
                                                            normal_payload.size(), normal_payload) == 0;
                std::snprintf(detail, sizeof(detail),
                              "落盘普通行 %zu 字节，尾部必须是 \"%s\"（%zu 字节）",
                              normal_line != nullptr ? normal_line->size() : 0u,
                              normal_payload.c_str(), normal_payload.size());
                Check(normal_ok, "log.file.sink.normal_line", detail);

                // spdlog pattern 前缀（时间戳/级别/线程）从普通行反推：前缀长度 = 整行长度 - 已知 payload 长度。
                // 同一线程、同一级别（info）下前缀等长；error 行的级别名多一个字符，因此单独按后缀判定。
                const ID::size_t prefix_len = normal_ok ? normal_line->size() - normal_payload.size() : 0;
                const auto payload_of = [prefix_len](const std::string& line, std::string* payload) noexcept
                {
                    if (line.size() <= prefix_len)
                    {
                        return false;
                    }
                    *payload = line.substr(prefix_len);
                    return true;
                };

                // ---- 第 2/3 行：截断规则 ----
                std::string ascii_payload;
                const std::string* const ascii_line = find_line(lines, kAsciiTag);
                const bool ascii_found = ascii_line != nullptr && payload_of(*ascii_line, &ascii_payload);
                const bool ascii_ok = ascii_found
                                   && ascii_payload.size() == kLineCapacity
                                   && ascii_payload.compare(kLineCapacity - 3u, 3u, "...") == 0;
                std::snprintf(detail, sizeof(detail),
                              "ASCII 行 payload=%zu 字节（期望 %u，尾部 \"...\"），pattern 前缀 %zu 字节",
                              ascii_found ? ascii_payload.size() : 0u, kLineCapacity, prefix_len);
                Check(prefix_len != 0u && ascii_ok, "log.file.sink.ascii_1024", detail);

                std::string utf8_payload;
                const std::string* const utf8_line = find_line(lines, kUtf8Tag);
                const bool utf8_found = utf8_line != nullptr && payload_of(*utf8_line, &utf8_payload);
                const bool utf8_tail = utf8_found && utf8_payload.size() >= 3u
                                    && utf8_payload.compare(utf8_payload.size() - 3u, 3u, "...") == 0;
                std::snprintf(detail, sizeof(detail),
                              "UTF-8 行 payload=%zu 字节（允许区间 %u..%u，截断回退最多 3 字节）",
                              utf8_found ? utf8_payload.size() : 0u, kLineCapacity - 3u, kLineCapacity);
                Check(utf8_tail && utf8_payload.size() >= kLineCapacity - 3u && utf8_payload.size() <= kLineCapacity,
                      "log.file.sink.utf8_truncated", detail);
                Check(utf8_found && is_valid_utf8(utf8_payload, 0u),
                      "log.file.sink.utf8_valid",
                      "截断后的 payload 必须仍是合法 UTF-8：不允许残留半个码点");

                // ---- 第 4 行：非法 level 的 %l 必须是 error，不能落到 critical/fatal ----
                const std::string* const illegal_line = find_line(lines, kIllegalPayload);
                const ID::size_t illegal_len = sizeof(kIllegalPayload) - 1u;
                const bool illegal_ok = illegal_line != nullptr
                                     && illegal_line->size() >= illegal_len
                                     && illegal_line->find("[error]") != std::string::npos
                                     && illegal_line->find("[critical]") == std::string::npos
                                     && illegal_line->compare(illegal_line->size() - illegal_len,
                                                              illegal_len, kIllegalPayload) == 0;
                std::snprintf(detail, sizeof(detail),
                              "write(999,...) 落盘行 %zu 字节：必须含 [error]、不含 [critical]，payload = \"%s\"",
                              illegal_line != nullptr ? illegal_line->size() : 0u, kIllegalPayload);
                Check(illegal_ok, "log.file.sink.illegal_level_is_error", detail);

                // ---- 第 5~9 行：nullptr / 零长段 / 1024 边界 ----
                // 边界行的期望 payload 直接按实现口径写全（category 前缀 + 填充 + 可能的 "..."），
                // 用整串相等判定，避免"只看首字节/尾字节"这类容易被别的行蹭过的弱判据
                const std::string file_payload_prefix = std::string("[") + kFileCategory + "] ";
                const std::string fit_expected  = file_payload_prefix + std::string(kFitMessageBytes, 'D');
                const std::string over_expected = file_payload_prefix
                                                + std::string(kLineCapacity - 3u - kFilePrefixBytes, 'E')
                                                + "...";

                ID::size_t null_hits     = 0;
                ID::size_t zero_cat_hits = 0;
                ID::size_t zero_msg_hits = 0;
                ID::size_t fit_hits      = 0;
                ID::size_t over_hits     = 0;
                for (const std::string& line : lines)
                {
                    if (line.find(kIllegalPayload) != std::string::npos)
                    {
                        continue;   // error 级别的前缀比 info 多一个字符，不参与按 info 前缀的 payload 比较
                    }
                    std::string payload;
                    if (!payload_of(line, &payload))
                    {
                        continue;
                    }
                    if (payload == kNullSegmentsPayload)
                    {
                        ++null_hits;
                    }
                    else if (payload == kZeroCategoryPayload)
                    {
                        ++zero_cat_hits;
                    }
                    else if (payload == kZeroMessagePayload)
                    {
                        ++zero_msg_hits;
                    }
                    else if (payload == fit_expected)
                    {
                        ++fit_hits;
                    }
                    else if (payload == over_expected)
                    {
                        ++over_hits;
                    }
                }

                std::snprintf(detail, sizeof(detail),
                              "category/message 同时 nullptr 的 payload 必须恰为 \"%s\"：命中 %zu 行",
                              kNullSegmentsPayload, null_hits);
                Check(null_hits == 1u, "log.file.sink.null_segments", detail);

                std::snprintf(detail, sizeof(detail),
                              "category_len=0 的 payload 必须恰为 \"%s\"（命中 %zu），message_len=0 的 payload "
                              "必须恰为 \"%s\"（命中 %zu）",
                              kZeroCategoryPayload, zero_cat_hits, kZeroMessagePayload, zero_msg_hits);
                Check(zero_cat_hits == 1u && zero_msg_hits == 1u, "log.file.sink.zero_lengths", detail);

                std::snprintf(detail, sizeof(detail),
                              "message_len=%u 时 payload 必须恰好 %u 字节且不截断（命中 %zu）；"
                              "message_len=%u 时必须截断到 %u 字节并以 \"...\" 结尾（命中 %zu）",
                              kFitMessageBytes, kLineCapacity, fit_hits,
                              kFitMessageBytes + 1u, kLineCapacity, over_hits);
                Check(fit_hits == 1u && over_hits == 1u, "log.file.sink.line_capacity_boundary", detail);

                // ---- 第 10 行：四次被拒的 reconfigure 之后旧后端仍写出了内容 ----
                const ID::size_t reject_hits = count_containing(lines, kRejectProbeMarker);
                std::snprintf(detail, sizeof(detail),
                              "四次被拒的 reconfigure 之后写入的标记必须落盘：命中 %zu 行（期望 1）", reject_hits);
                Check(reject_hits == 1u, "log.file.sink.writable_after_rejects", detail);
            }

            Check(std::remove(file_path) == 0, "log.file.sink.cleanup", "用例结束时必须能删除临时日志文件");
        }

        // =================================================================
        // 轮 1d：4 线程 × 100 条同步写入（多线程生产 → 落盘行数）
        // =================================================================
        {
            char concurrent_path[kMaxFilePathBytes] = {};
            temp_path(concurrent_path, sizeof(concurrent_path), kConcurrentName);
            const bool path_ok = std::strlen(concurrent_path) < kMaxFilePathBytes - 1;
            std::remove(concurrent_path);

            LogConfig concurrent_config{};
            concurrent_config.min_level      = level_value(ID::Log::Level::Trace);
            concurrent_config.enable_console = false;
            concurrent_config.enable_file    = true;
            concurrent_config.async          = false;   // 同步形态：一次写调用直接落到 sink，不存在异步投递计数
            concurrent_config.queue_size     = 0;
            std::snprintf(concurrent_config.file_path, sizeof(concurrent_config.file_path), "%s", concurrent_path);

            const ReconfigureOutcome to_file = try_reconfigure(table, concurrent_config);
            Check(path_ok && to_file.accepted && to_file.file_after && !to_file.async_after,
                  "log.concurrent.reconfigure",
                  "4 线程用例要求 file + sync 后端：reconfigure 必须接受 enable_file=true + async=false");

            constexpr ID::uint32_t kProducers      = 4;
            constexpr ID::uint32_t kLinesPerThread = 100;
            constexpr ID::uint32_t kExpectedLines  = kProducers * kLinesPerThread;

            if (logger != nullptr)
            {
                std::thread producers[kProducers];
                for (ID::uint32_t tid = 0; tid < kProducers; ++tid)
                {
                    producers[tid] = std::thread([logger, tid]() noexcept
                    {
                        for (ID::uint32_t i = 0; i < kLinesPerThread; ++i)
                        {
                            logger->info("concurrent {} {}", tid, i);
                        }
                    });
                }
                for (ID::uint32_t tid = 0; tid < kProducers; ++tid)
                {
                    producers[tid].join();
                }
            }
            table->flush();     // 同步形态：返回时内容已交给 OS

            LogConfig console_config{};
            const ReconfigureOutcome to_console = try_reconfigure(table, console_config);
            Check(to_console.accepted && to_console.console_after && to_console.async_after && !to_console.file_after,
                  "log.concurrent.file_closed",
                  "回到 console + async 必须成功：旧文件后端被排空并关闭");

            LineList     lines;
            const bool   opened = read_lines(concurrent_path, &lines);
            ID::uint32_t per_thread[kProducers] = {};
            ID::uint32_t concurrent_lines       = 0;
            if (opened)
            {
                for (const std::string& line : lines)
                {
                    if (line.find(kConcurrentMarker) == std::string::npos)
                    {
                        continue;
                    }
                    ++concurrent_lines;
                    for (ID::uint32_t tid = 0; tid < kProducers; ++tid)
                    {
                        char needle[32];
                        std::snprintf(needle, sizeof(needle), "concurrent %u ", tid);
                        if (line.find(needle) != std::string::npos)
                        {
                            ++per_thread[tid];
                            break;
                        }
                    }
                }
            }

            char detail[420];
            std::snprintf(detail, sizeof(detail),
                          "4 线程 × 100 条同步写入后文件共 %zu 行（其中 concurrent 行 %u），"
                          "按线程 0/1/2/3 分别 %u/%u/%u/%u（期望各 100，总计 %u）",
                          lines.size(), concurrent_lines,
                          per_thread[0], per_thread[1], per_thread[2], per_thread[3], kExpectedLines);
            const bool counts_ok = concurrent_lines == kExpectedLines
                                && lines.size() == kExpectedLines
                                && per_thread[0] == kLinesPerThread
                                && per_thread[1] == kLinesPerThread
                                && per_thread[2] == kLinesPerThread
                                && per_thread[3] == kLinesPerThread;
            Check(opened && counts_ok, "log.concurrent.lines_400", detail);
            Check(std::remove(concurrent_path) == 0, "log.concurrent.cleanup",
                  "用例结束必须能删除并发用例日志文件");
        }

        // =================================================================
        // 轮 1e：轮转（小 max_file_bytes + max_files，同步写多行 → 断言文件集合与单文件上限）
        // 前提固定为 spdlog rotating_file_sink_mt 的两条行为：轮转文件名 `<stem>.<index><ext>`
        // （index 从 1 起）、**先轮转后写**（所以最新一行一定落在主文件）；前提不成立时下面的
        // 断言必须失败，而不是被"只要有几个文件就算过"掩盖
        // =================================================================
        {
            char rotation_path[kMaxFilePathBytes] = {};
            temp_path(rotation_path, sizeof(rotation_path), kRotationBaseName);
            const bool path_ok = std::strlen(rotation_path) < kMaxFilePathBytes - 1;
            static_cast<void>(remove_rotation_set());     // 清掉上一轮残留，保证结论可重复

            LogConfig rotation_config{};
            rotation_config.min_level      = level_value(ID::Log::Level::Trace);
            rotation_config.enable_console = false;
            rotation_config.enable_file    = true;
            rotation_config.async          = false;
            rotation_config.queue_size     = 0;
            rotation_config.max_file_bytes = kRotationMaxBytes;
            rotation_config.max_files      = kRotationMaxFiles;
            std::snprintf(rotation_config.file_path, sizeof(rotation_config.file_path), "%s", rotation_path);

            const ReconfigureOutcome to_rotation = try_reconfigure(table, rotation_config);
            Check(path_ok && to_rotation.accepted && to_rotation.file_after && !to_rotation.async_after,
                  "log.rotation.reconfigure",
                  "max_file_bytes=512 + max_files=3 的同步文件后端必须被 reconfigure 接受");

            constexpr ID::uint32_t kRotationLines = 40;
            for (ID::uint32_t i = 0; i < kRotationLines; ++i)
            {
                char      message[64];
                const int written = std::snprintf(message, sizeof(message), "%s%02u", kRotationMarker, i);
                table->write(level_value(ID::Log::Level::Info), kFileCategory, kFileCategoryBytes,
                             message, static_cast<ID::uint32_t>(written));
            }
            table->flush();

            LogConfig console_config{};
            const ReconfigureOutcome to_console = try_reconfigure(table, console_config);
            Check(to_console.accepted && to_console.console_after && !to_console.file_after,
                  "log.rotation.file_closed",
                  "回到 console + async 必须成功：轮转文件 sink 在此关闭");

            char rotated1[kMaxFilePathBytes] = {};
            char rotated2[kMaxFilePathBytes] = {};
            char rotated3[kMaxFilePathBytes] = {};
            char rotated4[kMaxFilePathBytes] = {};
            rotated_path(rotated1, sizeof(rotated1), 1u);
            rotated_path(rotated2, sizeof(rotated2), 2u);
            rotated_path(rotated3, sizeof(rotated3), 3u);
            rotated_path(rotated4, sizeof(rotated4), kRotationCleanupFiles);

            const long long base_size     = file_size(rotation_path);
            const long long rotated1_size = file_size(rotated1);
            const long long rotated2_size = file_size(rotated2);
            const long long rotated3_size = file_size(rotated3);
            const long long rotated4_size = file_size(rotated4);      // max_files=3 → .4 必须不存在

            LineList base_lines;
            static_cast<void>(read_lines(rotation_path, &base_lines));
            const ID::size_t last_message_hits = count_containing(base_lines, "rotate line 39");

            char detail[640];
            std::snprintf(detail, sizeof(detail),
                          "前提：rotating_file_sink_mt 先轮转后写、轮转名 <stem>.<n>%s（n 从 1 起）；"
                          "主文件 %lld 字节（上限 %u）、.1=%lld、.2=%lld、.3=%lld、"
                          ".%u=%lld（max_files=%u，必须不存在）；主文件含末条 \"rotate line 39\" 的行数 %zu（期望 1）",
                          kRotationExt, base_size, static_cast<ID::uint32_t>(kRotationMaxBytes),
                          rotated1_size, rotated2_size, rotated3_size,
                          static_cast<unsigned>(kRotationCleanupFiles), rotated4_size,
                          static_cast<unsigned>(kRotationMaxFiles), last_message_hits);

            const long long max_bytes = static_cast<long long>(kRotationMaxBytes);
            const bool sizes_ok = base_size > 0 && base_size <= max_bytes
                               && rotated1_size > 0 && rotated1_size <= max_bytes
                               && rotated2_size > 0 && rotated2_size <= max_bytes
                               && rotated3_size > 0 && rotated3_size <= max_bytes
                               && rotated4_size < 0;
            Check(sizes_ok && last_message_hits == 1u, "log.rotation.files_and_cap", detail);

            const bool cleanup_ok = remove_rotation_set();
            Check(cleanup_ok, "log.rotation.cleanup",
                  "轮转文件集合（base/.1/.2/.3，外加必须不存在的 .4）必须能全部删除，重复运行才能得到同样结论");
        }

        // flush 在域内已被多次真实调用（同步形态 flush sink、异步形态投递屏障）。
        // 它没有进程内可观测的完成点：异步下不等待屏障、fflush 也不等于 fsync，
        // 所以"调用返回且不崩溃"写成 Check(true) 只是恒真断言，这里改为 Skip 并写明落盘证据在哪
        table->flush();
        Skip("log.flush.callable",
             "异步 flush 只投递屏障、不等待完成（fflush 不是 fsync，也不排空队列），进程内没有可观测的完成点；"
             "把'调用返回且不崩溃'写成 Check(true) 是恒真断言，因此不做断言。"
             "落盘证据由 log.file.sink.* / log.concurrent.lines_400 / log.rotation.files_and_cap 的读回断言提供");

        // ---------- 关闭：服务表必须随之注销 ----------
        const bool round_one_stopped = stop_product("log.engine.shutdown");
        if (round_one_stopped)
        {
            Check(ID::PluginRuntime::instance().get_service<LogApiTable>(ID::ServiceSlot::Log) == nullptr,
                  "log.service.revoked",
                  "on_unload 注销服务表后 ServiceSlot::Log 必须为空");
        }

        // =================================================================
        // 轮 2：acquire_logger 的 64 槽容量边界（on_init 自占 1 槽，可申请 63 个）
        // =================================================================
        {
            const LogApiTable* const capacity_table = start_product("log.engine.start_slot_round");
            if (capacity_table == nullptr)
            {
                return;
            }

            constexpr ID::uint32_t kExpectedHandles = kMaxLoggerHandles - 1;    // on_init 已经占用 1 个槽
            const char kSlotCategory[] = "Bench.Log.Slot";
            const ID::uint32_t kSlotCategoryBytes = static_cast<ID::uint32_t>(sizeof(kSlotCategory) - 1);

            Logger*      handles[kExpectedHandles] = {};
            ID::uint32_t acquired                = 0;

            // 最多探 65 次：63 个槽都拿到之后紧接着的一次必须是 nullptr
            for (ID::uint32_t probe = 0; probe <= kMaxLoggerHandles; ++probe)
            {
                Logger* const handle = capacity_table->acquire_logger(kSlotCategory, kSlotCategoryBytes);
                if (handle == nullptr)
                {
                    break;
                }
                if (acquired < kExpectedHandles)
                {
                    handles[acquired] = handle;
                }
                ++acquired;
            }

            Check(acquired == kExpectedHandles, "log.logger.slot_capacity_63",
                  "on_init 自占 1 槽，其余必须恰好可申请 63 个，再申请即 nullptr");
            Check(handles_are_distinct(handles, acquired < kExpectedHandles ? acquired : kExpectedHandles),
                  "log.logger.slots_distinct",
                  "每个句柄必须指向互不相同的槽位（不做 category 查重，但槽位不复用）");

            if (acquired != 0 && acquired <= kExpectedHandles)
            {
                ID_LOG_INFO(handles[acquired - 1], "slot boundary probe {}", acquired);
            }

            static_cast<void>(stop_product("log.engine.shutdown_slot_round"));
        }

        // =================================================================
        // 轮 3：shutdown 之后重新 start（多轮）+ 幂等 shutdown + 槽位复位
        // =================================================================
        {
            const LogApiTable* const restart_table = start_product("log.engine.restart_start");
            if (restart_table == nullptr)
            {
                return;
            }

            Logger* const restarted = ID::Log::acquire_logger(*restart_table, "Bench.Log.Restart");
            Check(restarted != nullptr, "log.logger.slots_reset_on_restart",
                  "shutdown 复位 64 槽：重新 start 后必须能再申请到句柄（否则第 64 轮起永远拿不到）");
            if (restarted != nullptr)
            {
                ID_LOG_INFO(restarted, "restart probe {}", 1);
            }

            const bool restart_stopped = stop_product("log.engine.restart_shutdown");
            if (restart_stopped)
            {
                Check(ID::PluginRuntime::instance().get_service<LogApiTable>(ID::ServiceSlot::Log) == nullptr,
                      "log.service.revoked_after_restart",
                      "每一轮 shutdown 之后服务槽都必须为空");
            }

            const ID::PluginRuntimeDiagnostic again = ID::Engine::shutdown();
            Check(again.is_ok() && ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Idle,
                  "log.engine.shutdown_idempotent",
                  "Idle 状态下再次 shutdown 必须是幂等无操作");
        }

        // =================================================================
        // 覆盖缺口：契约要求子进程隔离或属宿主契约的路径逐条跳过并写明依据
        // =================================================================
        Skip("log.write.fatal",
             "Level::Fatal 走同步 logger + flush + std::abort()：契约要求子进程隔离，"
             "官方进程内不触发；IDLogFatalValidation（validation/log）覆盖");
        Skip("log.logger.stale_handle",
             "句柄只在 Ready 区间有效；shutdown 后解引用是契约外空指针崩溃，需子进程隔离，"
             "官方进程内不测；IDLogFatalValidation（validation/log）覆盖");
        Skip("log.reconfigure.concurrent_shutdown",
             "reconfigure/configure/get_stats 与 shutdown 并发属宿主契约（禁止并发），"
             "需要专门的多线程门控设计，本域不覆盖");
        Skip("log.queue.overrun_count",
             "overrun 计数要求生产者在 worker 消费之前把队列填满，属竞态压测：console sink 写 stderr 的"
             "消费速度不受控、本域也没有可控的慢 sink，无法在同一进程内确定性构造饱和，因此不断言计数；"
             "空队列计数为 0 已由 log.stats.empty_queue_counters 覆盖，队列容量的合法性由 "
             "log.reconfigure.reject_queue_size 覆盖");

        Check(ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Idle,
              "log.domain.idle_after",
              "本域结束后运行期必须是 Idle，后续域才能拿到干净状态");
    }
} // namespace ID::Bench

#else

namespace ID::Bench
{
    void IDLogSecurityTest() noexcept
    {
        // 域必须始终有实现：未选中 ID.Log 的 profile 下不产生任何 DUT 访问，只说明为什么没测
        Skip("ID.Log", "product profile does not select ID.Log");
    }
} // namespace ID::Bench

#endif
