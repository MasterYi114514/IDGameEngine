#include "IDLog.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "benchmark_support.hpp"

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

/**
 *  IDLog 正确性用例（按公开头与 README 写明的契约断言，不按实现猜测）。
 *  
 *  取证方式：
 *  - 文件目标：写入后读回文件，按行数对账（内容完整性、flush 证据）。
 *  - 终端目标：把进程 stdout 的文件描述符临时重定向到临时文件，读回后按标记文本判定可见性
 *    （报告输出与日志输出共用 stdout，不重定向就无法区分两者）；重定向建立失败时该用例记 Skip，
 *    绝不向真实控制台写日志。
 *  - fatal 与陈旧指针：同进程取证会杀掉/污染整个测试进程，由父进程按参数分裂自身，
 *    按子进程退出码与该子进程实际落盘内容判定。
 *  - 违约用法的可观察行为（destroy_logger 之后的陈旧 `Logger*`、`shutdown` 之后重新 init 的句柄别名 ABA）
 *    按"实现行为"断言，不写成契约承诺：公开头与 README 明确不承诺安全、也不检测句柄跨轮复用。
 *  
 *  纪律：
 *  - 每个用例自包含地建立前置状态（自己 init、自己 shutdown），不依赖用例顺序，单跑不误判；
 *  - 重定向窗口内不产生本设施的输出（否则控制台与报告会不一致）；
 *  - 失败用 Check 记录并继续，不用断言宏终止进程。
 */

namespace
{
    using ID::Log::Level;
    using ID::Log::Logger;
    using ID::Log::LoggerHandle;
    using ID::Log::SinkType;

    // ---- 非法运行期参数（契约：README 第 2.3、6 节的"参数越界"） ----
    //
    // `Level` 是运行期参数：强制转换后超出 `Fatal`（取值 >= 6）时 `set_sink_level` 返回 false，
    // 且不改动任何阈值（不"就近归一"成 Trace 或其它合法阈值）。`SinkType` 同理（超出 Console/File）。
    constexpr Level    kInvalidLevelSix  = static_cast<Level>(6);
    constexpr Level    kInvalidLevel255  = static_cast<Level>(255);
    constexpr SinkType kInvalidSinkType  = static_cast<SinkType>(9);

    // ---- 退出码约定（子进程用返回值收尾时代表"前置失败"，与崩溃/终止区分） ----

    constexpr int kFatalChildPrepFailed       = 0xA1;   // fatal 子进程：日志器/参数准备失败
    constexpr int kFatalChildFileFailed       = 0xA2;   // fatal 子进程：文件目标启用失败
    constexpr int kFatalChildNotTerminated    = 0xA3;   // fatal 子进程：未按契约终止
    constexpr int kStaleChildPrepFailed       = 0xB1;   // 陈旧指针子进程：日志器/参数准备失败
    constexpr int kStaleChildFileFailed       = 0xB2;   // 陈旧指针子进程：文件目标启用失败
    // 已销毁日志器的四个配置方法：子进程按返回码区分哪个方法没有按契约返回 false
    constexpr int kDestroyedChildPrepFailed    = 0xC1;      // 日志器/参数准备失败
    constexpr int kDestroyedChildFileFailed    = 0xC2;      // 文件目标启用失败（前置）
    constexpr int kDestroyedChildEnableFailed  = 0xC3;      // enable_sink 未返回 false
    constexpr int kDestroyedChildDisableFailed = 0xC4;      // disable_sink 未返回 false
    constexpr int kDestroyedChildLevelFailed   = 0xC5;      // set_sink_level 未返回 false
    constexpr int kDestroyedChildPathFailed    = 0xC6;      // set_sink_path 未返回 false

#if defined(_WIN32)
    // 本平台（MinGW-w64 + UCRT）实测：std::abort() 经 __fastfail 终止进程，退出码 0xC0000409
    constexpr unsigned long kExpectedAbortExitCode  = 0xC0000409UL;
    constexpr bool          kHasExpectedAbortCode   = true;
#else
    // 非本平台：未配置 abort 的期望退出码，只断言"异常终止"，具体码按未覆盖登记
    constexpr unsigned long kExpectedAbortExitCode  = 0UL;
    constexpr bool          kHasExpectedAbortCode   = false;
#endif

    // ---- 文件与文本工具 ----

    void RemoveFileQuiet(const std::string& path) noexcept
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    bool ReadWholeFile(const std::string& path, std::string& out) noexcept
    {
        out.clear();
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }

        char buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            out.append(buffer, read);
        }

        std::fclose(file);
        return true;
    }

    void WriteWholeFile(const std::string& path, std::string_view text) noexcept
    {
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            return;
        }

        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }

    std::size_t CountLines(const std::string& text) noexcept
    {
        std::size_t count = 0;
        for (const char symbol : text)
        {
            if (symbol == '\n')
            {
                ++count;
            }
        }

        if (!text.empty() && text.back() != '\n')
        {
            ++count;
        }
        return count;
    }

    bool ContainsText(const std::string& text, std::string_view needle) noexcept
    {
        return text.find(needle) != std::string::npos;
    }

    std::size_t CountText(const std::string& text, std::string_view needle) noexcept
    {
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = text.find(needle, position)) != std::string::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    // 是否存在同一行同时含两个片段：用于断言"这一行的级别名确实是 X"（跨行命中不算通过）
    bool LineContainsBoth(const std::string& text, std::string_view first, std::string_view second) noexcept
    {
        std::size_t begin = 0;
        while (begin < text.size())
        {
            std::size_t end = text.find('\n', begin);
            if (end == std::string::npos)
            {
                end = text.size();
            }

            const std::string_view line(text.data() + begin, end - begin);
            if (line.find(first) != std::string_view::npos && line.find(second) != std::string_view::npos)
            {
                return true;
            }

            begin = end + 1;
        }
        return false;
    }

    // ---- 子进程隔离 ----

    struct ChildOutcome
    {
        bool            m_spawned   = false;
        bool            m_timed_out = false;
        unsigned long   m_exit_code = 0;
    };

    /**
     *  @brief 以同一可执行文件的分裂模式跑一个子进程，捕获其 stdout/stderr 到独立文件
     *  @param child_argument 传给子进程的第一个参数（例如 "--fatal-child=<log path>"）
     *  @param child_stdout_path 子进程 stdout/stderr 的捕获文件
     *  @note 返回码集合由调用方判定；本函数只收集事实，不解释
     */
    ChildOutcome RunChildProcess(const std::string& child_argument,
                                 const std::string& child_stdout_path) noexcept
    {
        ChildOutcome outcome;

#if defined(_WIN32)
        char exe_path[MAX_PATH] = {};
        const DWORD exe_length = ::GetModuleFileNameA(nullptr, exe_path, sizeof(exe_path));
        if (exe_length == 0 || exe_length >= sizeof(exe_path))
        {
            return outcome;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE output_handle = ::CreateFileA(child_stdout_path.c_str(), GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output_handle == INVALID_HANDLE_VALUE)
        {
            return outcome;
        }

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output_handle;
        startup.hStdError = output_handle;
        startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

        // 子进程的输出与官方报告无关：重定向到独立文件，既不污染本进程 stdout，也可作为证据
        std::string command_line = std::string("\"") + exe_path + "\" \"" + child_argument + "\"";

        PROCESS_INFORMATION process{};
        std::fflush(stdout);
        const BOOL created = ::CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0,
                                             nullptr, nullptr, &startup, &process);
        ::CloseHandle(output_handle);
        if (created == FALSE)
        {
            return outcome;
        }
        outcome.m_spawned = true;

        if (::WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            if (::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE)
            {
                outcome.m_exit_code = static_cast<unsigned long>(exit_code);
            }
        }
        else
        {
            // 契约要求 fatal 终止进程；超时说明未终止，先清理再如实记录
            outcome.m_timed_out = true;
            ::TerminateProcess(process.hProcess, 1);
            ::WaitForSingleObject(process.hProcess, 5000);
        }

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
#else
        (void)child_argument;
        (void)child_stdout_path;
#endif

        return outcome;
    }

    // ---- 各用例 ----

    // 前置：未初始化状态（未初始化时 shutdown 是契约规定的无操作）
    void CaseUninitializedState() noexcept
    {
        ID::Log::shutdown();

        const LoggerHandle handle = ID::Log::create_logger("pre-init");
        ID::Bench::Check(!handle.is_valid(), "create.before_init_invalid",
                         "契约：未 init 时 create_logger 返回无效句柄（不隐式初始化）");
        ID::Bench::Check(ID::Log::get_logger(handle) == nullptr, "get.invalid_handle_null",
                         "无效句柄 get_logger 返回 nullptr");

        ID::Log::destroy_logger(handle);
        ID::Bench::Check(ID::Log::get_logger(handle) == nullptr, "destroy.invalid_handle_noop",
                         "无效句柄 destroy_logger 是无操作，且不破坏登记状态");

        ID::Log::shutdown();     // 收尾：本用例自包含，不把状态留给下一个用例
    }

    void CaseInitAndRegistrationChain() noexcept
    {
        ID::Log::init();
        ID::Log::init();        // 幂等：重复 init 不得改变就绪状态

        const LoggerHandle handle = ID::Log::create_logger("chain");
        ID::Bench::Check(handle.is_valid(), "init.idempotent",
                         "两次 init() 后 create_logger 仍返回有效句柄（幂等）");

        Logger* logger = ID::Log::get_logger(handle);
        ID::Bench::Check(logger != nullptr, "create.get_chain",
                         "create_logger → get_logger 返回非空指针");
        if (logger != nullptr)
        {
            // 报告输出与终端目标共用 stdout：本用例关闭终端目标，避免日志行混进报告
            logger->disable_sink(SinkType::Console);
            logger->info("chain message {}", 1);
        }

        ID::Log::destroy_logger(handle);
        ID::Bench::Check(ID::Log::get_logger(handle) == nullptr, "destroy.stale_handle_null",
                         "契约：destroy_logger 之后该句柄 get_logger 返回 nullptr");

        ID::Log::destroy_logger(handle);
        ID::Bench::Check(ID::Log::get_logger(handle) == nullptr, "destroy.repeat_safe",
                         "契约：重复 destroy_logger 安全，不重复回收槽位");

        ID::Log::shutdown();
    }

    // 自包含：本用例自己 init，自己 shutdown（不依赖别的用例留下的就绪状态）
    void CaseNullNameLogger() noexcept
    {
        ID::Log::init();

        const LoggerHandle handle = ID::Log::create_logger(nullptr);
        ID::Bench::Check(handle.is_valid(), "create.null_name_valid",
                         "契约：name 可为 nullptr，按空名处理并返回有效句柄");

        Logger* logger = ID::Log::get_logger(handle);
        ID::Bench::Check(logger != nullptr, "create.null_name_usable",
                         "空名日志器可取到对象并可用");
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
        }

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();
    }

    // 自包含：自己 init，不显式 destroy（由 shutdown 负责 flush 与清理）
    void CaseShutdownCleanup() noexcept
    {
        constexpr int kMessageCount = 500;
        const std::string path = ID::Bench::TempPath("shutdown_flush.log");
        RemoveFileQuiet(path);

        ID::Log::init();

        const LoggerHandle handle = ID::Log::create_logger("shutdown");
        Logger* logger = ID::Log::get_logger(handle);
        bool enabled = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            enabled = logger->set_sink_path(SinkType::File, path.c_str());
            for (int index = 0; index < kMessageCount; ++index)
            {
                logger->info("shutdown-line {}", index);
            }
        }
        ID::Bench::Check(enabled, "file.path_enable_usable",
                         "临时目录下的合法路径 set_sink_path(File) 返回 true");

        ID::Log::shutdown();

        std::string content;
        const bool read_ok = ReadWholeFile(path, content);
        ID::Bench::Check(read_ok, "shutdown.file_readable",
                         "shutdown 后日志文件仍可读");
        ID::Bench::Check(CountLines(content) == static_cast<std::size_t>(kMessageCount),
                         "shutdown.flush_complete",
                         "契约：shutdown 先 flush 再销毁，500 条消息全部交付文件流");

        ID::Bench::Check(ID::Log::get_logger(handle) == nullptr, "shutdown.handle_invalid",
                         "契约：shutdown 使全部句柄失效，旧句柄取不到对象");

        const LoggerHandle after_shutdown = ID::Log::create_logger("after-shutdown");
        ID::Bench::Check(!after_shutdown.is_valid(), "shutdown.create_invalid",
                         "契约：shutdown 后未 init 时 create_logger 返回无效句柄");

        // 未初始化时再次 shutdown 应是 no-op
        ID::Log::shutdown();
        ID::Bench::Check(!ID::Log::create_logger("no-init").is_valid(), "shutdown.uninitialized_noop",
                         "契约：未初始化时 shutdown 是无操作，就绪状态不变");

        ID::Log::init();
        const LoggerHandle reopened = ID::Log::create_logger("reopened");
        ID::Bench::Check(reopened.is_valid(), "init.after_shutdown",
                         "契约：shutdown 之后可以再次 init() 并创建日志器");
        ID::Log::destroy_logger(reopened);
        ID::Log::shutdown();
    }

    // 仅文件可见：终端阈值为 Fatal（只放行 critical），文件阈值为 Trace
    void CaseFileOnlyVisibility() noexcept
    {
        const std::string file_path = ID::Bench::TempPath("trim_file_only.log");
        const std::string capture_path = ID::Bench::TempPath("trim_file_only_stdout.log");
        RemoveFileQuiet(file_path);
        RemoveFileQuiet(capture_path);

        ID::Log::init();

        ID::Bench::StdoutRedirect redirect;
        if (!redirect.Begin(capture_path.c_str()))
        {
            ID::Log::shutdown();
            ID::Bench::Skip("trim.file_only_visibility",
                            "平台/环境不适用：stdout 重定向无法建立，终端可见性无法与报告输出分离取证；"
                            "不在真实控制台上造日志，本用例不触发契约判定");
            return;
        }

        const LoggerHandle handle = ID::Log::create_logger("trim-file-only");
        Logger* logger = ID::Log::get_logger(handle);
        bool enabled = false;
        if (logger != nullptr)
        {
            logger->set_sink_level(SinkType::Console, Level::Fatal);
            logger->set_sink_level(SinkType::File, Level::Trace);
            enabled = logger->set_sink_path(SinkType::File, file_path.c_str());
            logger->info("visible-in-file-only {}", 1);
            logger->error("error-leg-in-file-only");
            logger->disable_sink(SinkType::File);         // 冲刷文件目标后摘除
            logger->disable_sink(SinkType::Console);      // 冲刷终端目标（此时 stdout 指向捕获文件）
        }
        redirect.End();

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(enabled, "trim.file_only_file_enabled", "文件目标启用成功");

        std::string file_content;
        std::string capture_content;
        const bool file_read = ReadWholeFile(file_path, file_content);
        const bool capture_read = ReadWholeFile(capture_path, capture_content);

        ID::Bench::Check(file_read && ContainsText(file_content, "visible-in-file-only 1"),
                         "trim.file_trace_passes_info",
                         "文件阈值 Trace：info 消息写入文件");
        ID::Bench::Check(file_read && ContainsText(file_content, "error-leg-in-file-only"),
                         "trim.file_trace_passes_error",
                         "文件阈值 Trace：error 消息写入文件");
        ID::Bench::Check(capture_read && !ContainsText(capture_content, "visible-in-file-only"),
                         "trim.console_fatal_drops_info",
                         "终端阈值 Fatal：同一条 info 不在终端目标出现（两目标阈值独立）");
        ID::Bench::Check(capture_read && !ContainsText(capture_content, "error-leg-in-file-only"),
                         "trim.console_fatal_drops_error",
                         "终端阈值 Fatal：error 也不在终端目标出现");
    }

    // 仅终端可见：终端阈值为 Trace，文件阈值为 Fatal（只放行 critical）
    void CaseConsoleOnlyVisibility() noexcept
    {
        const std::string file_path = ID::Bench::TempPath("trim_console_only.log");
        const std::string capture_path = ID::Bench::TempPath("trim_console_only_stdout.log");
        RemoveFileQuiet(file_path);
        RemoveFileQuiet(capture_path);

        ID::Log::init();

        ID::Bench::StdoutRedirect redirect;
        if (!redirect.Begin(capture_path.c_str()))
        {
            ID::Log::shutdown();
            ID::Bench::Skip("trim.console_only_visibility",
                            "平台/环境不适用：stdout 重定向无法建立，终端可见性无法与报告输出分离取证；"
                            "不在真实控制台上造日志，本用例不触发契约判定");
            return;
        }

        const LoggerHandle handle = ID::Log::create_logger("trim-console-only");
        Logger* logger = ID::Log::get_logger(handle);
        bool enabled = false;
        if (logger != nullptr)
        {
            logger->set_sink_level(SinkType::Console, Level::Trace);
            logger->set_sink_level(SinkType::File, Level::Fatal);
            enabled = logger->set_sink_path(SinkType::File, file_path.c_str());
            logger->info("visible-in-console-only {}", 7);
            logger->disable_sink(SinkType::File);
            logger->disable_sink(SinkType::Console);
        }
        redirect.End();

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(enabled, "trim.console_only_file_enabled", "文件目标启用成功");

        std::string file_content;
        std::string capture_content;
        const bool file_read = ReadWholeFile(file_path, file_content);
        const bool capture_read = ReadWholeFile(capture_path, capture_content);

        ID::Bench::Check(capture_read && ContainsText(capture_content, "visible-in-console-only 7"),
                         "trim.console_trace_passes_info",
                         "终端阈值 Trace：info 消息出现在终端目标");
        ID::Bench::Check(file_read && !ContainsText(file_content, "visible-in-console-only"),
                         "trim.file_fatal_drops_info",
                         "文件阈值 Fatal：同一条 info 未写入文件（两目标阈值独立）");
    }

    void CaseBadPathKeepsOldTarget() noexcept
    {
        const std::string good_path = ID::Bench::TempPath("badpath_good.log");
        const std::string blocker_path = ID::Bench::TempPath("badpath_blocker");
        RemoveFileQuiet(good_path);
        WriteWholeFile(blocker_path, "blocker");

        // 父路径是普通文件：目录无法创建，文件无法打开
        const std::string bad_path = blocker_path + "/sub/log.txt";

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("bad-path");
        Logger* logger = ID::Log::get_logger(handle);
        bool good_enabled = false;
        bool bad_result = true;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            good_enabled = logger->set_sink_path(SinkType::File, good_path.c_str());
            logger->info("before-bad-path");
            bad_result = logger->set_sink_path(SinkType::File, bad_path.c_str());
            logger->info("after-bad-path");
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(good_enabled, "file.path_good_enabled", "合法路径先启用成功");
        ID::Bench::Check(!bad_result, "file.path_bad_false",
                         "父路径是普通文件时 set_sink_path(File) 返回 false（异常不跨 DLL 边界）");

        std::string content;
        const bool read_ok = ReadWholeFile(good_path, content);
        ID::Bench::Check(read_ok && ContainsText(content, "before-bad-path")
                             && ContainsText(content, "after-bad-path"),
                         "file.path_failure_keeps_old_target",
                         "契约：失败时已有配置保持可用，旧文件目标在坏路径失败后仍写入");
    }

    void CaseSamePathIdempotent() noexcept
    {
        const std::string path = ID::Bench::TempPath("same_path.log");
        RemoveFileQuiet(path);

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("same-path");
        Logger* logger = ID::Log::get_logger(handle);
        bool first = false;
        bool second = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            first = logger->set_sink_path(SinkType::File, path.c_str());
            logger->info("same-path-first");
            second = logger->set_sink_path(SinkType::File, path.c_str());     // 同路径重复启用
            logger->info("same-path-second");
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(first && second, "file.path_same_idempotent",
                         "契约：同一路径重复 set_sink_path(File) 两次都返回 true");

        std::string content;
        const bool read_ok = ReadWholeFile(path, content);
        ID::Bench::Check(read_ok && CountLines(content) == 2, "file.path_same_single_sink",
                         "同路径重复启用不得重复挂载 sink：文件恰好 2 行（各出现一次）");
        ID::Bench::Check(CountText(content, "same-path-second") == 1, "file.path_same_no_dup",
                         "重复启用后写入的消息只出现一次");
    }

    // 契约：同路径重新启用是追加写入，不截断已有内容
    void CaseFileReenableAppends() noexcept
    {
        const std::string path = ID::Bench::TempPath("reenable_append.log");
        RemoveFileQuiet(path);
        WriteWholeFile(path, "preexisting-line-kept\n");

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("reenable");
        Logger* logger = ID::Log::get_logger(handle);
        bool first = false;
        bool second = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            first = logger->set_sink_path(SinkType::File, path.c_str());
            logger->info("append-before-disable");
            logger->disable_sink(SinkType::File);
            second = logger->set_sink_path(SinkType::File, path.c_str());     // 停用后重新启用同一路径
            logger->info("append-after-reenable");
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(first && second, "file.reenable_after_disable_true",
                         "契约：disable_sink(File) 之后重新 set_sink_path(File) 同一路径返回 true");

        std::string content;
        const bool read_ok = ReadWholeFile(path, content);
        ID::Bench::Check(read_ok && ContainsText(content, "preexisting-line-kept"),
                         "file.reenable_no_truncate",
                         "契约：追加写入不截断——启用前已有的文件内容仍在");
        ID::Bench::Check(read_ok && ContainsText(content, "append-before-disable")
                             && ContainsText(content, "append-after-reenable"),
                         "file.reenable_both_segments",
                         "重新启用前后两段写入都在同一文件内（追加而非重写）");
    }

    void CaseSwitchPath() noexcept
    {
        const std::string first_path = ID::Bench::TempPath("switch_first.log");
        const std::string second_path = ID::Bench::TempPath("switch_second.log");
        RemoveFileQuiet(first_path);
        RemoveFileQuiet(second_path);

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("switch");
        Logger* logger = ID::Log::get_logger(handle);
        bool ok = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            const bool first_ok = logger->set_sink_path(SinkType::File, first_path.c_str());
            logger->info("switch-first-log");
            const bool second_ok = logger->set_sink_path(SinkType::File, second_path.c_str());
            logger->info("switch-second-log");
            logger->disable_sink(SinkType::File);
            ok = first_ok && second_ok;
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(ok, "file.path_switch", "异路径切换两次 set_sink_path(File) 都返回 true");

        std::string first_content;
        std::string second_content;
        const bool first_read = ReadWholeFile(first_path, first_content);
        const bool second_read = ReadWholeFile(second_path, second_content);

        ID::Bench::Check(first_read && ContainsText(first_content, "switch-first-log")
                             && !ContainsText(first_content, "switch-second-log"),
                         "file.switch_old_file_complete",
                         "切换前写入的消息在旧文件完整保留，且不出现切换后的消息");
        ID::Bench::Check(second_read && ContainsText(second_content, "switch-second-log")
                             && !ContainsText(second_content, "switch-first-log"),
                         "file.switch_new_file_isolated",
                         "切换后的消息只写新文件");
    }

    // 契约（README §2.3）：路径为空、文件打不开时返回 false，且已有配置保持可用
    void CaseEnableFileInvalidArgs() noexcept
    {
        const std::string good_path = ID::Bench::TempPath("invalidargs_good.log");
        RemoveFileQuiet(good_path);

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("invalid-args");
        Logger* logger = ID::Log::get_logger(handle);
        bool good_enabled = false;
        bool null_false = true;
        bool empty_false = true;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            good_enabled = logger->set_sink_path(SinkType::File, good_path.c_str());
            null_false = !logger->set_sink_path(SinkType::File, nullptr);
            empty_false = !logger->set_sink_path(SinkType::File, "");
            logger->info("after-invalid-args");
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(good_enabled, "file.path_invalid_args_precondition",
                         "前置：合法路径先启用成功（用于验证失败不改变已有配置）");
        ID::Bench::Check(null_false, "file.path_null_false",
                         "契约：path 为 nullptr 时 set_sink_path(File) 返回 false");
        ID::Bench::Check(empty_false, "file.path_empty_false",
                         "契约：path 为空串时 set_sink_path(File) 返回 false");

        std::string content;
        const bool read_ok = ReadWholeFile(good_path, content);
        ID::Bench::Check(read_ok && ContainsText(content, "after-invalid-args"),
                         "file.path_invalid_args_keeps_old_target",
                         "契约：两次非法参数失败后，旧文件目标仍在工作");
    }

    // 契约（README §2.3）：File 目标在从未成功设置路径时 enable_sink 返回 false——属于"未配置"而非"配置失败"，
    // 不创建目标、不改变任何已启用目标；set_sink_path 成功过之后 enable_sink(File) 复用已创建目标
    void CaseFileEnableWithoutPath() noexcept
    {
        const std::string capture_path = ID::Bench::TempPath("file_no_path_stdout.log");
        const std::string file_path    = ID::Bench::TempPath("file_no_path.log");
        RemoveFileQuiet(capture_path);
        RemoveFileQuiet(file_path);

        ID::Log::init();

        ID::Bench::StdoutRedirect redirect;
        if (!redirect.Begin(capture_path.c_str()))
        {
            ID::Log::shutdown();
            ID::Bench::Skip("file.enable_without_path",
                            "平台/环境不适用：stdout 重定向无法建立，无法证明失败调用后终端目标仍工作；"
                            "不在真实控制台上造日志，本用例不触发契约判定");
            return;
        }

        const LoggerHandle handle = ID::Log::create_logger("file-no-path");
        Logger* logger = ID::Log::get_logger(handle);
        bool enable_false = false;
        bool path_ok      = false;
        bool reenable_ok  = false;
        if (logger != nullptr)
        {
            logger->set_sink_level(SinkType::Console, Level::Trace);
            // 未设置路径：enable_sink(File) 不可启用，且不得改动已启用的终端目标
            enable_false = !logger->enable_sink(SinkType::File);
            logger->info("console-alive-after-failed-enable");
            path_ok = logger->set_sink_path(SinkType::File, file_path.c_str());
            logger->disable_sink(SinkType::File);
            reenable_ok = logger->enable_sink(SinkType::File);     // 已创建的目标可再次启用
            logger->info("file-alive-after-reenable");
            logger->disable_sink(SinkType::File);
            logger->disable_sink(SinkType::Console);
        }
        redirect.End();

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(enable_false, "file.enable_without_path_false",
                         "契约：从未设置路径时 enable_sink(File) 返回 false");
        ID::Bench::Check(path_ok, "file.enable_without_path_precondition",
                         "前置：随后 set_sink_path(File) 成功（失败调用没有留下坏目标）");
        ID::Bench::Check(reenable_ok, "file.enable_after_created_true",
                         "契约：set_sink_path 成功过之后 enable_sink(File) 返回 true（复用已创建目标）");

        std::string capture_content;
        std::string file_content;
        const bool capture_read = ReadWholeFile(capture_path, capture_content);
        const bool file_read    = ReadWholeFile(file_path, file_content);
        ID::Bench::Check(capture_read && ContainsText(capture_content, "console-alive-after-failed-enable"),
                         "file.enable_without_path_keeps_console",
                         "契约：失败调用不改变现有配置——终端目标仍输出消息");
        ID::Bench::Check(file_read && ContainsText(file_content, "file-alive-after-reenable")
                             && !ContainsText(file_content, "console-alive-after-failed-enable"),
                         "file.enable_after_created_writes",
                         "重新启用的文件目标收到新消息，且不含文件目标创建之前写入的消息");
    }

    // 契约（README §2.3）：只有 File 目标接受路径；Console 传入路径返回 false，且不改变已有配置
    void CaseConsolePathRejected() noexcept
    {
        const std::string file_path    = ID::Bench::TempPath("console_path_file.log");
        const std::string console_path = ID::Bench::TempPath("console_path_rejected.log");
        RemoveFileQuiet(file_path);
        RemoveFileQuiet(console_path);

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("console-path");
        Logger* logger = ID::Log::get_logger(handle);
        bool file_ok  = false;
        bool rejected = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            file_ok = logger->set_sink_path(SinkType::File, file_path.c_str());
            rejected = !logger->set_sink_path(SinkType::Console, console_path.c_str());
            logger->info("after-console-path-rejected");
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(file_ok, "console.path_rejected_precondition",
                         "前置：文件目标先启用成功（用于验证拒绝不改变已有配置）");
        ID::Bench::Check(rejected, "console.path_rejected",
                         "契约：set_sink_path(Console, path) 返回 false（只有 File 目标有路径）");
        ID::Bench::Check(!std::filesystem::exists(console_path), "console.path_rejected_no_file",
                         "契约：被拒绝的调用不创建该路径的文件");

        std::string content;
        const bool read_ok = ReadWholeFile(file_path, content);
        ID::Bench::Check(read_ok && ContainsText(content, "after-console-path-rejected"),
                         "console.path_rejected_file_still_works",
                         "契约：拒绝后已有文件目标仍在工作");
    }

    // 契约（README §9）：把非法的 SinkType 值强制转换后传入，四个配置方法都返回 false，
    // 不会静默当成 Console 或 File 处理（用后续消息仍按原阈值输出作为"没有被改动"的证据）
    void CaseInvalidSinkType() noexcept
    {
        const std::string file_path = ID::Bench::TempPath("invalid_type_file.log");
        RemoveFileQuiet(file_path);

        const SinkType invalid_type = kInvalidSinkType;

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("invalid-type");
        Logger* logger = ID::Log::get_logger(handle);
        bool file_ok       = false;
        bool enable_false  = true;
        bool disable_false = true;
        bool level_false   = true;
        bool path_false    = true;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            file_ok = logger->set_sink_path(SinkType::File, file_path.c_str());
            enable_false = !logger->enable_sink(invalid_type);
            disable_false = !logger->disable_sink(invalid_type);
            level_false = !logger->set_sink_level(invalid_type, Level::Fatal);
            path_false = !logger->set_sink_path(invalid_type, file_path.c_str());
            logger->info("after-invalid-type");     // 文件阈值仍为 Trace：非法调用没有改动任何目标
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(file_ok, "sink.invalid_type_precondition",
                         "前置：文件目标先启用成功（用于验证非法调用不改变已有配置）");
        ID::Bench::Check(enable_false, "sink.invalid_type_enable_false",
                         "契约：非法 SinkType 传入 enable_sink 返回 false");
        ID::Bench::Check(disable_false, "sink.invalid_type_disable_false",
                         "契约：非法 SinkType 传入 disable_sink 返回 false（不会误摘除真实目标）");
        ID::Bench::Check(level_false, "sink.invalid_type_level_false",
                         "契约：非法 SinkType 传入 set_sink_level 返回 false（不会误改真实目标阈值）");
        ID::Bench::Check(path_false, "sink.invalid_type_path_false",
                         "契约：非法 SinkType 传入 set_sink_path 返回 false");

        std::string content;
        const bool read_ok = ReadWholeFile(file_path, content);
        ID::Bench::Check(read_ok && ContainsText(content, "after-invalid-type"),
                         "sink.invalid_type_targets_unchanged",
                         "契约：四次非法调用之后文件目标仍按 Trace 阈值工作（配置未被改动）");
    }

    // 契约（README §2.3、§6"参数越界"）：`set_sink_level` 收到非法 `Level`（强制转换后超出 `Fatal`）返回 false，
    // 且不改动任何阈值——不"就近归一"成 Trace 或其它合法阈值。
    // 覆盖两个目标 × 三种目标状态：未创建（File 从未设置路径）、已启用、已停用（摘除后重新启用仍复用原阈值）。
    // 每个拒绝都用"随后写入的 Info 仍放行、Debug 仍被裁剪"作为阈值未被改动的证据。
    void CaseInvalidLevelRejected() noexcept
    {
        const std::string capture_path        = ID::Bench::TempPath("invalid_level_stdout.log");
        const std::string file_uncreated_path = ID::Bench::TempPath("invalid_level_uncreated.log");
        const std::string file_enabled_path   = ID::Bench::TempPath("invalid_level_enabled.log");
        RemoveFileQuiet(capture_path);
        RemoveFileQuiet(file_uncreated_path);
        RemoveFileQuiet(file_enabled_path);

        ID::Log::init();

        ID::Bench::StdoutRedirect redirect;
        if (!redirect.Begin(capture_path.c_str()))
        {
            ID::Log::shutdown();
            ID::Bench::Skip("sink.invalid_level",
                            "平台/环境不适用：stdout 重定向无法建立，终端目标的阈值是否被改动无法取证；"
                            "不在真实控制台上造日志，本用例不触发契约判定");
            return;
        }

        const LoggerHandle handle = ID::Log::create_logger("invalid-level");
        Logger* logger = ID::Log::get_logger(handle);

        bool level_precondition         = false;
        bool file_uncreated_six_false   = false;
        bool file_uncreated_255_false   = false;
        bool console_enabled_six_false  = false;
        bool console_enabled_255_false  = false;
        bool console_disabled_six_false = false;
        bool console_disabled_255_false = false;
        bool file_enabled_six_false     = false;
        bool file_enabled_255_false     = false;
        bool file_uncreated_ok          = false;
        bool file_enabled_ok            = false;

        if (logger != nullptr)
        {
            // 状态 1：目标未创建（File 从未设置路径）——先记一个合法阈值 Info，再用非法 Level 尝试覆盖
            logger->set_sink_level(SinkType::File, Level::Info);
            file_uncreated_six_false = !logger->set_sink_level(SinkType::File, kInvalidLevelSix);
            file_uncreated_255_false = !logger->set_sink_level(SinkType::File, kInvalidLevel255);
            file_uncreated_ok        = logger->set_sink_path(SinkType::File, file_uncreated_path.c_str());
            logger->debug("file-uncreated-debug-dropped");
            logger->info("file-uncreated-info-passed");
            logger->disable_sink(SinkType::File);

            // 状态 2：目标已启用（Console 在 create_logger 时已启用，先把阈值提到 Info）
            const bool console_level_info_ok = logger->set_sink_level(SinkType::Console, Level::Info);
            console_enabled_six_false  = !logger->set_sink_level(SinkType::Console, kInvalidLevelSix);
            console_enabled_255_false  = !logger->set_sink_level(SinkType::Console, kInvalidLevel255);
            logger->debug("console-enabled-debug-dropped");
            logger->info("console-enabled-info-passed");

            // 状态 3：目标已停用（摘除后用非法 Level 尝试覆盖，再启用验证阈值没有被重置成默认 Trace）
            logger->disable_sink(SinkType::Console);
            console_disabled_six_false = !logger->set_sink_level(SinkType::Console, kInvalidLevelSix);
            console_disabled_255_false = !logger->set_sink_level(SinkType::Console, kInvalidLevel255);
            logger->enable_sink(SinkType::Console);
            logger->debug("console-disabled-debug-dropped");
            logger->info("console-disabled-info-passed");
            logger->disable_sink(SinkType::Console);

            // 状态 2'：File 目标已启用（挂载状态下再收非法 Level）
            file_enabled_ok        = logger->set_sink_path(SinkType::File, file_enabled_path.c_str());
            file_enabled_six_false = !logger->set_sink_level(SinkType::File, kInvalidLevelSix);
            file_enabled_255_false = !logger->set_sink_level(SinkType::File, kInvalidLevel255);
            logger->debug("file-enabled-debug-dropped");
            logger->info("file-enabled-info-passed");
            logger->disable_sink(SinkType::File);

            level_precondition = console_level_info_ok && file_uncreated_ok && file_enabled_ok;
        }
        redirect.End();

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(level_precondition, "sink.invalid_level_precondition",
                         "前置：两个目标先设合法阈值 Level::Info、文件目标两次启用成功");

        ID::Bench::Check(file_uncreated_six_false && file_uncreated_255_false,
                         "sink.invalid_level_file_uncreated_false",
                         "契约：File 目标尚未创建时，非法 Level 6 / 255 传给 set_sink_level 都返回 false");
        ID::Bench::Check(console_enabled_six_false && console_enabled_255_false,
                         "sink.invalid_level_console_enabled_false",
                         "契约：Console 目标已启用时，非法 Level 6 / 255 传给 set_sink_level 都返回 false");
        ID::Bench::Check(console_disabled_six_false && console_disabled_255_false,
                         "sink.invalid_level_console_disabled_false",
                         "契约：Console 目标已停用时，非法 Level 6 / 255 传给 set_sink_level 都返回 false");
        ID::Bench::Check(file_enabled_six_false && file_enabled_255_false,
                         "sink.invalid_level_file_enabled_false",
                         "契约：File 目标已启用时，非法 Level 6 / 255 传给 set_sink_level 都返回 false");

        std::string capture_content;
        std::string uncreated_content;
        std::string enabled_content;
        const bool capture_read   = ReadWholeFile(capture_path, capture_content);
        const bool uncreated_read = ReadWholeFile(file_uncreated_path, uncreated_content);
        const bool enabled_read   = ReadWholeFile(file_enabled_path, enabled_content);

        ID::Bench::Check(capture_read
                             && ContainsText(capture_content, "console-enabled-info-passed")
                             && !ContainsText(capture_content, "console-enabled-debug-dropped"),
                         "sink.invalid_level_console_enabled_threshold_kept",
                         "契约：已启用目标收到非法 Level 后阈值仍是 Info——Info 放行、Debug 仍被裁剪");
        ID::Bench::Check(capture_read
                             && ContainsText(capture_content, "console-disabled-info-passed")
                             && !ContainsText(capture_content, "console-disabled-debug-dropped"),
                         "sink.invalid_level_console_disabled_threshold_kept",
                         "契约：已停用目标收到非法 Level 后阈值仍是 Info（重新启用时生效），未被重置为默认 Trace");
        ID::Bench::Check(uncreated_read
                             && ContainsText(uncreated_content, "file-uncreated-info-passed")
                             && !ContainsText(uncreated_content, "file-uncreated-debug-dropped"),
                         "sink.invalid_level_file_uncreated_threshold_kept",
                         "契约：目标未创建时记下的阈值不受非法 Level 影响——创建后按 Info 生效（Debug 被裁剪）");
        ID::Bench::Check(enabled_read
                             && ContainsText(enabled_content, "file-enabled-info-passed")
                             && !ContainsText(enabled_content, "file-enabled-debug-dropped"),
                         "sink.invalid_level_file_enabled_threshold_kept",
                         "契约：已启用文件目标的阈值不受非法 Level 影响——Info 落盘、Debug 未落盘");
    }

    // 契约（README §2.3、§6）：两个运行期参数同时越界（非法 `SinkType` + 非法 `Level`）时同样以 false 拒绝：
    // 不创建目标、不改动任何已挂载目标与阈值，返回值是唯一观测渠道。
    void CaseInvalidLevelWithInvalidSinkType() noexcept
    {
        const std::string capture_path = ID::Bench::TempPath("invalid_level_type_stdout.log");
        const std::string file_path    = ID::Bench::TempPath("invalid_level_type_file.log");
        RemoveFileQuiet(capture_path);
        RemoveFileQuiet(file_path);

        ID::Log::init();

        ID::Bench::StdoutRedirect redirect;
        if (!redirect.Begin(capture_path.c_str()))
        {
            ID::Log::shutdown();
            ID::Bench::Skip("sink.invalid_level_and_type",
                            "平台/环境不适用：stdout 重定向无法建立，终端目标的输出与阈值无法取证；"
                            "不在真实控制台上造日志，本用例不触发契约判定");
            return;
        }

        const LoggerHandle handle = ID::Log::create_logger("invalid-level-type");
        Logger* logger = ID::Log::get_logger(handle);

        bool level_precondition      = false;
        bool combo_six_false         = false;
        bool combo_255_false         = false;
        bool file_enable_still_false = false;
        bool file_target_absent      = false;

        if (logger != nullptr)
        {
            level_precondition = logger->set_sink_level(SinkType::Console, Level::Info);
            combo_six_false    = !logger->set_sink_level(kInvalidSinkType, kInvalidLevelSix);
            combo_255_false    = !logger->set_sink_level(kInvalidSinkType, kInvalidLevel255);
            logger->debug("combo-debug-dropped");
            logger->info("combo-info-passed");
            // 组合非法调用不得创建文件目标：从未设置路径，因此 enable_sink(File) 仍返回 false
            file_enable_still_false = !logger->enable_sink(SinkType::File);
            file_target_absent      = !std::filesystem::exists(file_path);
            logger->disable_sink(SinkType::Console);
        }
        redirect.End();

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(level_precondition, "sink.invalid_level_and_type_precondition",
                         "前置：Console 阈值先设为合法值 Level::Info");

        ID::Bench::Check(combo_six_false && combo_255_false, "sink.invalid_level_and_type_false",
                         "契约：非法 SinkType 与非法 Level 同时传入 set_sink_level 返回 false"
                         "（6 与 255 两种越界值都拒绝）");

        std::string capture_content;
        const bool capture_read = ReadWholeFile(capture_path, capture_content);
        ID::Bench::Check(capture_read
                             && ContainsText(capture_content, "combo-info-passed")
                             && !ContainsText(capture_content, "combo-debug-dropped"),
                         "sink.invalid_level_and_type_config_unchanged",
                         "契约：组合非法调用后终端目标仍按 Info 工作（Info 放行、Debug 被裁剪）");
        ID::Bench::Check(file_enable_still_false && file_target_absent,
                         "sink.invalid_level_and_type_no_target_created",
                         "契约：组合非法调用不创建文件目标——enable_sink(File) 仍返回 false，且该路径未生成文件");
    }

    // 现状固化（不是契约承诺）：句柄不带代数/版本位（README 第 5、6 节），`LoggerHandle` 只属于当前那一轮
    // `init` / `shutdown` 生命周期。shutdown → init 之后槽位下标从 0 重新分配，上一轮句柄的数值会被新日志器
    // 复用（ABA）：`get_logger(旧句柄)` 返回新日志器而不是 nullptr，调用方无法据返回值分辨。
    // 契约要求调用方必须重新 `create_logger`；本用例只固化"当前实现确实如此"，不改动被测实现，
    // 也不把该行为当成可依赖的契约。
    void CaseReinitHandleAliasCurrentBehavior() noexcept
    {
        const std::string path = ID::Bench::TempPath("reinit_alias.log");
        RemoveFileQuiet(path);

        ID::Log::init();
        const LoggerHandle old_handle = ID::Log::create_logger("a");
        const bool old_valid = old_handle.is_valid();
        ID::Log::shutdown();

        ID::Log::init();
        const LoggerHandle new_handle = ID::Log::create_logger("b");

        const bool alias_id_equal = old_valid && new_handle.is_valid()
                                    && (old_handle.get_id() == new_handle.get_id());

        // 旧句柄取到的是新日志器：用它配置文件目标并写一条消息，按模式串的 %n 判定名称确实是 "b"
        Logger* alias_logger = ID::Log::get_logger(old_handle);
        const bool alias_non_null = (alias_logger != nullptr);
        if (alias_logger != nullptr)
        {
            alias_logger->disable_sink(SinkType::Console);
            alias_logger->set_sink_level(SinkType::File, Level::Trace);
            alias_logger->set_sink_path(SinkType::File, path.c_str());
            alias_logger->info("reinit-alias-marker");
            alias_logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(new_handle);
        ID::Log::shutdown();

        std::string content;
        const bool read_ok = ReadWholeFile(path, content);

        ID::Bench::Check(alias_id_equal, "handle.reinit_alias_id_equal",
                         "现状（ABA，契约不承诺）：shutdown→init→create 之后新句柄数值等于上一轮旧句柄数值");
        ID::Bench::Check(alias_non_null, "handle.reinit_alias_get_logger_non_null",
                         "现状（ABA）：shutdown→init 后 get_logger(旧句柄) 返回非空指针（命中新日志器）而不是 nullptr，"
                         "因此不能用它判断句柄属于哪一轮");
        ID::Bench::Check(read_ok
                             && LineContainsBoth(content, "[b]", "reinit-alias-marker")
                             && !ContainsText(content, "[a]"),
                         "handle.reinit_alias_points_to_new_logger",
                         "现状（ABA）：旧句柄取到的对象写出的行中 %n 为 b、旧日志器名 a 未出现，"
                         "证明旧句柄数值已指向新日志器；调用方必须重新 create_logger");
    }

    // 竞态期间登记过的句柄（每个成功创建的句柄都记下来，用于"关闭后残留"抽样）
    struct RaceRecord
    {
        std::mutex                  m_mutex;
        std::vector<LoggerHandle>   m_handles;
    };

    // 并发防御压测：产品侧契约前提是 `init` / `shutdown` 由产品侧串行调用（README 第 5 节），
    // 本用例故意打破该前提——多线程持续 `create_logger`，另一线程循环 shutdown/init，验证实现的锁内重查防御
    // （公开头写明：后端构造在锁外，插入前在锁内重新确认仍处于已初始化状态，构造期间被 shutdown 则返回无效句柄）。
    // 判定不变量（全程只用句柄与 get_logger 的 null 与否，不解引用任何可能失效的 `Logger*`——那是 UB）：
    //  1. 每次 shutdown 之后（未初始化窗口内）抽样本轮登记过的句柄：一律取不到对象——不出现"关闭后仍登记残留"；
    //  2. 竞态结束、干净 shutdown 之后抽样全部记录句柄：同样全部为 nullptr；
    //  3. 随后 init 的第一次 create_logger 槽位下标是 0：登记表已复位，池计数没有跨轮累积；
    //  4. `create_logger` 允许返回无效句柄（契约允许），本用例不把"返回无效"当失败。
    void CaseCreateShutdownRaceDefense() noexcept
    {
        constexpr int         kRaceThreads         = 4;
        constexpr int         kMaxRaceRounds       = 64;
        constexpr int         kCreatesPerThread    = 4000;
        constexpr double      kInitializedDwellNs  = 2'000'000.0;    // 每轮已初始化窗口：让 create 真正登记
        constexpr std::size_t kResidualSamples     = 32;
        constexpr int         kResidualSpins       = 64;

        RaceRecord                record;
        std::atomic<long long>    valid_creates{ 0 };
        std::atomic<long long>    invalid_creates{ 0 };
        std::atomic<long long>    invalid_in_window{ 0 };   // 近似：返回无效句柄且调用区间落在 shutdown 窗口内
        std::atomic<long long>    shutdown_begin_ns{ 0 };
        std::atomic<long long>    shutdown_end_ns{ 0 };
        std::atomic<int>          ready_threads{ 0 };
        std::atomic<int>          finished_threads{ 0 };

        record.m_handles.reserve(static_cast<std::size_t>(kRaceThreads) * kCreatesPerThread);

        std::vector<std::thread> threads;
        threads.reserve(kRaceThreads);
        for (int thread_index = 0; thread_index < kRaceThreads; ++thread_index)
        {
            threads.emplace_back([&record, &valid_creates, &invalid_creates, &invalid_in_window,
                                  &shutdown_begin_ns, &shutdown_end_ns, &ready_threads,
                                  &finished_threads]() noexcept {
                ID::Bench::SetAffinity(-1);     // 本用例不测耗时：解绑即可，绑核失败不必告警
                ready_threads.fetch_add(1, std::memory_order_release);

                for (int index = 0; index < kCreatesPerThread; ++index)
                {
                    const double call_begin = ID::Bench::NowNs();
                    const LoggerHandle handle = ID::Log::create_logger("race");
                    const double call_end = ID::Bench::NowNs();
                    if (handle.is_valid())
                    {
                        valid_creates.fetch_add(1, std::memory_order_relaxed);
                        {
                            // 保留句柄（不销毁）：句柄存活才能让"关闭后残留"在关闭窗口内可观测
                            const std::lock_guard<std::mutex> lock(record.m_mutex);
                            record.m_handles.push_back(handle);
                        }
                    }
                    else
                    {
                        invalid_creates.fetch_add(1, std::memory_order_relaxed);
                        // 只统计"调用区间与 shutdown 窗口重叠"的无效返回：这是锁内重查路径（或紧随其后的
                        // fast-path 拒绝）被实际压到的证据；上一轮留下的窗口时间戳必然早于本轮调用，不会误命中
                        const double window_begin = static_cast<double>(shutdown_begin_ns.load(std::memory_order_relaxed));
                        const double window_end   = static_cast<double>(shutdown_end_ns.load(std::memory_order_relaxed));
                        if (window_end > 0.0 && call_begin <= window_end && call_end >= window_begin)
                        {
                            invalid_in_window.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                finished_threads.fetch_add(1, std::memory_order_release);
            });
        }

        ID::Log::init();
        while (ready_threads.load(std::memory_order_acquire) < kRaceThreads)
        {
            std::this_thread::yield();
        }

        std::size_t round_residual_hits = 0;
        int         rounds_done         = 0;
        while (rounds_done < kMaxRaceRounds
               && finished_threads.load(std::memory_order_acquire) < kRaceThreads)
        {
            // 已初始化窗口：停一会儿让 create 真正登记成功，否则整个压测只压到 fast-path 拒绝
            const double dwell_begin = ID::Bench::NowNs();
            while ((ID::Bench::NowNs() - dwell_begin) < kInitializedDwellNs)
            {
                std::this_thread::yield();
            }

            shutdown_begin_ns.store(static_cast<long long>(ID::Bench::NowNs()), std::memory_order_relaxed);
            ID::Log::shutdown();
            shutdown_end_ns.store(static_cast<long long>(ID::Bench::NowNs()), std::memory_order_relaxed);

            // 未初始化窗口：此时能取到对象的句柄就是"关闭后仍登记残留"（防御缺失的签名）
            for (int spin = 0; spin < kResidualSpins && round_residual_hits == 0; ++spin)
            {
                std::vector<LoggerHandle> sample;
                {
                    const std::lock_guard<std::mutex> lock(record.m_mutex);
                    const std::size_t available = record.m_handles.size();
                    const std::size_t count     = (available < kResidualSamples) ? available : kResidualSamples;
                    sample.assign(record.m_handles.end() - static_cast<std::ptrdiff_t>(count),
                                  record.m_handles.end());
                }

                for (const LoggerHandle candidate : sample)
                {
                    if (ID::Log::get_logger(candidate) != nullptr)
                    {
                        ++round_residual_hits;
                        break;
                    }
                }
            }

            ID::Log::init();
            ++rounds_done;
        }

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        // 收尾：一次干净的 shutdown 后抽样全部记录句柄，再确认登记表复位
        ID::Log::shutdown();
        std::size_t final_residual_hits = 0;
        {
            const std::lock_guard<std::mutex> lock(record.m_mutex);
            for (const LoggerHandle candidate : record.m_handles)
            {
                if (ID::Log::get_logger(candidate) != nullptr)
                {
                    ++final_residual_hits;
                }
            }
        }

        ID::Log::init();
        const LoggerHandle post_race = ID::Log::create_logger("post-race");
        const bool pool_reset = post_race.is_valid() && (post_race.get_id() == 0U);
        ID::Log::destroy_logger(post_race);
        ID::Log::shutdown();

        const long long valid_count   = valid_creates.load(std::memory_order_relaxed);
        const long long invalid_count = invalid_creates.load(std::memory_order_relaxed);
        const long long window_count  = invalid_in_window.load(std::memory_order_relaxed);

        if (valid_count == 0)
        {
            ID::Bench::Warn("race.no_registration_observed",
                            "本轮竞态期间没有任何 create_logger 返回有效句柄：已初始化窗口没被压到，"
                            "残留抽样的样本量为 0，防御路径只由槽位复位检查间接验证");
        }

        char detail[256] = {};
        std::snprintf(detail, sizeof(detail),
                      "%d 轮 shutdown/init × %d 线程各 %d 次 create：有效句柄 %lld / 无效句柄 %lld；"
                      "关闭窗口内残留命中 %zu 次、收尾抽样残留 %zu 次",
                      rounds_done, kRaceThreads, kCreatesPerThread, valid_count, invalid_count,
                      round_residual_hits, final_residual_hits);
        ID::Bench::Check(round_residual_hits == 0 && final_residual_hits == 0,
                         "race.shutdown_no_residual_registration", detail);
        ID::Bench::Check(pool_reset, "race.pool_reset_after_shutdown",
                         "契约（shutdown 复位登记状态）：竞态结束、干净 shutdown + init 之后，"
                         "第一次 create_logger 的槽位下标为 0——登记表已清空，池计数没有跨轮累积");

        if (window_count > 0)
        {
            char window_detail[256] = {};
            std::snprintf(window_detail, sizeof(window_detail),
                          "近似口径：%lld 次 create_logger 返回无效句柄且其调用区间与 shutdown 窗口重叠；"
                          "无法区分 fast-path 拒绝与锁内重查拒绝，故只作为“该窗口被实际压到”的证据",
                          window_count);
            ID::Bench::Check(true, "race.shutdown_window_observed", window_detail);
        }
        else
        {
            ID::Bench::Uncovered("race.shutdown_window_observed",
                                 "未覆盖：本轮没有观测到调用区间与 shutdown 窗口重叠的 create_logger 返回"
                                 "（近似口径）；竞态窗口未被实际压到，"
                                 "防御路径只由“关闭后无残留 + 槽位复位”间接验证");
        }
    }

    // 契约（README §2.3）：日志器已销毁时四个配置方法返回 false；用陈旧 Logger* 调用属于违约用法，
    // 实现行为是返回 false 且不崩溃（契约不承诺安全）——同进程取证若崩溃会带走整个测试进程，
    // 因此由子进程隔离
    void CaseDestroyedLoggerConfig() noexcept
    {
#if defined(_WIN32)
        const std::string child_stdout_path = ID::Bench::TempPath("destroyed_config_child_stdout.log");
        RemoveFileQuiet(child_stdout_path);

        const ChildOutcome outcome = RunChildProcess("--destroyed-config-child", child_stdout_path);
        ID::Bench::Check(outcome.m_spawned, "sink.destroyed_child_spawned",
                         "子进程创建成功（陈旧指针调用可能崩溃，必须跨进程取证）");
        if (!outcome.m_spawned)
        {
            return;
        }

        ID::Bench::Check(!outcome.m_timed_out, "sink.destroyed_child_terminates",
                         "子进程在超时前结束（没有卡在失效对象上）");
        char exit_detail[224] = {};
        std::snprintf(exit_detail, sizeof(exit_detail),
                      "子进程退出码=0x%08lX；期望=0x00000000（实现行为：四个方法都返回 false 并正常收尾，"
                      "违约用法、契约不承诺安全）；"
                      "非 0 时按 0xC1 准备/0xC2 文件前置/0xC3 enable/0xC4 disable/0xC5 level/0xC6 path 定位",
                      outcome.m_exit_code);
        ID::Bench::Check(outcome.m_exit_code == 0UL, "sink.destroyed_config_methods_false", exit_detail);
#else
        ID::Bench::Uncovered("sink.destroyed_config_methods_false",
                             "非 Windows 平台未实现子进程隔离（未声称 Linux 支持）："
                             "已销毁日志器的配置调用未取证");
#endif
    }

    // 契约（README §2.3）：enable_sink(Console) 复用已创建目标的阈值——停用再启用不回到默认 Trace
    void CaseConsoleLevelReuse() noexcept
    {
        const std::string capture_path = ID::Bench::TempPath("console_reuse_stdout.log");
        RemoveFileQuiet(capture_path);

        ID::Log::init();

        ID::Bench::StdoutRedirect redirect;
        if (!redirect.Begin(capture_path.c_str()))
        {
            ID::Log::shutdown();
            ID::Bench::Skip("console.level_reuse",
                            "平台/环境不适用：stdout 重定向无法建立，终端目标输出无法取证；"
                            "不在真实控制台上造日志，本用例不触发契约判定");
            return;
        }

        const LoggerHandle handle = ID::Log::create_logger("console-reuse");
        Logger* logger = ID::Log::get_logger(handle);
        if (logger != nullptr)
        {
            logger->set_sink_level(SinkType::Console, Level::Warn);  // 从默认 Trace 提到 Warn
            logger->disable_sink(SinkType::Console);                 // 摘除目标，阈值应保留
            logger->enable_sink(SinkType::Console);                  // 复用同一目标与其阈值
            logger->info("reuse-should-be-dropped");
            logger->warn("reuse-should-be-visible");
            logger->disable_sink(SinkType::Console);
        }
        redirect.End();

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        std::string capture_content;
        const bool capture_read = ReadWholeFile(capture_path, capture_content);
        ID::Bench::Check(capture_read && ContainsText(capture_content, "reuse-should-be-visible"),
                         "console.level_reuse_warn_visible",
                         "重新启用后 Warn 级消息仍输出（阈值复用）");
        ID::Bench::Check(capture_read && !ContainsText(capture_content, "reuse-should-be-dropped"),
                         "console.level_reuse_info_dropped",
                         "重新启用后 Info 级仍被 Warn 阈值裁剪（未回到默认 Trace）");
    }

    // 契约（README §2.1/§2.3）：多参数格式化按 fmt 语义展开；trace/debug/warn 各自映射到对应级别名
    void CaseFormatArgumentsAndLevelNames() noexcept
    {
        const std::string path = ID::Bench::TempPath("format_levels.log");
        RemoveFileQuiet(path);

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("format-levels");
        Logger* logger = ID::Log::get_logger(handle);
        bool enabled = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            enabled = logger->set_sink_path(SinkType::File, path.c_str());
            logger->info("this is an example {}, {}", 1, "two");
            logger->trace("trace-marker");
            logger->debug("debug-marker");
            logger->warn("warn-marker");
            logger->error("error-marker");
            logger->disable_sink(SinkType::File);
        }
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        ID::Bench::Check(enabled, "format.file_enabled", "前置：文件目标启用成功");

        std::string content;
        const bool read_ok = ReadWholeFile(path, content);
        ID::Bench::Check(read_ok && ContainsText(content, "this is an example 1, two"),
                         "format.multi_args",
                         "契约：多参数 fmt 格式化 info(\"this is an example {}, {}\", 1, \"two\") 输出含 1 与 two");
        ID::Bench::Check(read_ok && LineContainsBoth(content, "[trace]", "trace-marker"),
                         "level.trace_name", "契约：trace 消息行级别名为 trace");
        ID::Bench::Check(read_ok && LineContainsBoth(content, "[debug]", "debug-marker"),
                         "level.debug_name", "契约：debug 消息行级别名为 debug");
        ID::Bench::Check(read_ok && LineContainsBoth(content, "[warning]", "warn-marker"),
                         "level.warn_name", "契约：Level::Warn 映射到 warn（模式串 %l 渲染为 warning）");
        ID::Bench::Check(read_ok && LineContainsBoth(content, "[error]", "error-marker"),
                         "level.error_name", "契约：Level::Error 映射到 err（模式串 %l 渲染为 error）");
    }

    void CaseMultiLoggerConcurrency() noexcept
    {
        constexpr int kLoggerCount = 4;
        constexpr int kMessagesPerLogger = 4000;

        ID::Log::init();

        LoggerHandle handles[kLoggerCount] = {};
        Logger* loggers[kLoggerCount] = {};
        std::string paths[kLoggerCount];
        bool setup_ok = true;

        for (int index = 0; index < kLoggerCount; ++index)
        {
            char file_name[48] = {};
            std::snprintf(file_name, sizeof(file_name), "concurrent_logger_%d.log", index);
            paths[index] = ID::Bench::TempPath(file_name);
            RemoveFileQuiet(paths[index]);

            handles[index] = ID::Log::create_logger("mt-logger");
            if (!handles[index].is_valid())
            {
                setup_ok = false;
            }
        }

        // 契约：`get_logger` 的指针只保证到下一次 create_logger / destroy_logger / shutdown 之前
        // （槽位数组扩容会使旧指针失效）。因此必须先建完全部句柄再取指针，取到后不再做登记操作。
        for (int index = 0; index < kLoggerCount; ++index)
        {
            loggers[index] = ID::Log::get_logger(handles[index]);
            if (loggers[index] == nullptr)
            {
                setup_ok = false;
                continue;
            }

            loggers[index]->disable_sink(SinkType::Console);
            loggers[index]->set_sink_level(SinkType::File, Level::Trace);
            if (!loggers[index]->set_sink_path(SinkType::File, paths[index].c_str()))
            {
                setup_ok = false;
            }
        }
        ID::Bench::Check(setup_ok, "concurrency.multi_logger_setup",
                         "4 个日志器及其文件目标创建成功");

        std::atomic<int> unpin_failures{ 0 };
        if (setup_ok)
        {
            std::vector<std::thread> threads;
            threads.reserve(kLoggerCount);
            for (int index = 0; index < kLoggerCount; ++index)
            {
                Logger* target = loggers[index];
                threads.emplace_back([target, &unpin_failures]() noexcept {
                    if (!ID::Bench::SetAffinity(-1))
                    {
                        unpin_failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    for (int message = 0; message < kMessagesPerLogger; ++message)
                    {
                        target->info("concurrent line {}", message);
                    }
                });
            }
            for (std::thread& thread : threads)
            {
                thread.join();
            }
        }

        // 解绑失败只影响噪声（多线程用例本就要求不绑核），如实记告警，不判失败
        const int unpinned_failed = unpin_failures.load(std::memory_order_relaxed);
        if (unpinned_failed > 0)
        {
            ID::Bench::Warn("affinity.thread_unpin_failed",
                            std::to_string(unpinned_failed) + " 个工作线程 SetAffinity(-1) 返回 false；"
                            "不影响行数判定");
        }

        for (int index = 0; index < kLoggerCount; ++index)
        {
            ID::Log::destroy_logger(handles[index]);
        }
        ID::Log::shutdown();

        std::size_t total_lines = 0;
        std::size_t expected_lines = 0;
        for (int index = 0; index < kLoggerCount; ++index)
        {
            std::string content;
            if (ReadWholeFile(paths[index], content))
            {
                total_lines += CountLines(content);
            }
            if (loggers[index] != nullptr)
            {
                expected_lines += static_cast<std::size_t>(kMessagesPerLogger);
            }
        }

        char detail[160] = {};
        std::snprintf(detail, sizeof(detail), "4 线程各写 1 个 logger × 4000 条：实际 %zu 行 / 期望 %zu 行",
                      total_lines, expected_lines);
        ID::Bench::Check(total_lines == expected_lines && expected_lines > 0,
                         "concurrency.multi_logger_lines", detail);
    }

    void CaseSingleLoggerConcurrency() noexcept
    {
        constexpr int kThreadCount = 4;
        constexpr int kMessagesPerThread = 2000;

        const std::string path = ID::Bench::TempPath("concurrent_single.log");
        RemoveFileQuiet(path);

        ID::Log::init();
        const LoggerHandle handle = ID::Log::create_logger("mt-single");
        Logger* logger = ID::Log::get_logger(handle);
        bool setup_ok = false;
        if (logger != nullptr)
        {
            logger->disable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::File, Level::Trace);
            setup_ok = logger->set_sink_path(SinkType::File, path.c_str());
        }
        ID::Bench::Check(setup_ok, "concurrency.single_logger_setup",
                         "同一 logger 的文件目标创建成功");

        std::atomic<int> unpin_failures{ 0 };
        if (setup_ok)
        {
            std::vector<std::thread> threads;
            threads.reserve(kThreadCount);
            for (int index = 0; index < kThreadCount; ++index)
            {
                threads.emplace_back([logger, &unpin_failures]() noexcept {
                    if (!ID::Bench::SetAffinity(-1))
                    {
                        unpin_failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    for (int message = 0; message < kMessagesPerThread; ++message)
                    {
                        logger->info("shared logger line {}", message);
                    }
                });
            }
            for (std::thread& thread : threads)
            {
                thread.join();
            }
        }

        const int unpinned_failed = unpin_failures.load(std::memory_order_relaxed);
        if (unpinned_failed > 0)
        {
            ID::Bench::Warn("affinity.thread_unpin_failed_single",
                            std::to_string(unpinned_failed) + " 个工作线程 SetAffinity(-1) 返回 false；"
                            "不影响行数判定");
        }

        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();

        std::string content;
        const bool read_ok = ReadWholeFile(path, content);
        const std::size_t expected = setup_ok ? static_cast<std::size_t>(kThreadCount * kMessagesPerThread) : 0;
        const std::size_t actual = read_ok ? CountLines(content) : 0;

        char detail[160] = {};
        std::snprintf(detail, sizeof(detail), "同一 logger、4 线程 × 2000 条：实际 %zu 行 / 期望 %zu 行",
                      actual, expected);
        ID::Bench::Check(expected > 0 && actual == expected, "concurrency.single_logger_lines", detail);
    }

    void CaseFatalTermination() noexcept
    {
#if defined(_WIN32)
        const std::string log_path = ID::Bench::TempPath("fatal_child.log");
        const std::string child_stdout_path = ID::Bench::TempPath("fatal_child_stdout.log");
        RemoveFileQuiet(log_path);
        RemoveFileQuiet(child_stdout_path);

        const ChildOutcome outcome = RunChildProcess("--fatal-child=" + log_path, child_stdout_path);
        ID::Bench::Check(outcome.m_spawned, "fatal.child_spawned",
                         "子进程创建成功（fatal 契约要求终止进程，必须跨进程取证）");
        if (!outcome.m_spawned)
        {
            return;
        }

        ID::Bench::Check(!outcome.m_timed_out, "fatal.child_terminates",
                         "契约：fatal 记日志后 std::abort()，子进程必须在超时前终止");

        // 子进程按返回值收尾的三条路径（0xA1 前置失败 / 0xA2 文件目标失败 / 0xA3 未终止）都不满足契约
        const bool is_child_return = (outcome.m_exit_code == static_cast<unsigned long>(kFatalChildPrepFailed))
                                     || (outcome.m_exit_code == static_cast<unsigned long>(kFatalChildFileFailed))
                                     || (outcome.m_exit_code == static_cast<unsigned long>(kFatalChildNotTerminated));
        ID::Bench::Check(!is_child_return, "fatal.exit_code_not_child_return",
                         "非正常退出：退出码不是子进程按返回值收尾的路径（0xA1/0xA2/0xA3）");

        if (kHasExpectedAbortCode)
        {
            char exit_detail[192] = {};
            std::snprintf(exit_detail, sizeof(exit_detail),
                          "子进程退出码=0x%08lX；本平台 abort 期望值=0x%08lX；超时=%s",
                          outcome.m_exit_code, kExpectedAbortExitCode, outcome.m_timed_out ? "是" : "否");
            ID::Bench::Check(outcome.m_exit_code == kExpectedAbortExitCode, "fatal.exit_code_abort",
                             exit_detail);
        }
        else
        {
            ID::Bench::Uncovered("fatal.exit_code_abort",
                                 "非本平台未配置 abort 的期望退出码：只断言异常终止，不断言具体退代码");
            ID::Bench::Check(outcome.m_exit_code != 0, "fatal.exit_code_abnormal",
                             "非本平台：只断言退出码非 0（异常终止），具体码未配置期望值");
        }

        std::string log_content;
        const bool log_read = ReadWholeFile(log_path, log_content);
        ID::Bench::Check(log_read, "fatal.log_file_exists",
                         "fatal 子进程的日志文件存在且可读");
        ID::Bench::Check(ContainsText(log_content, "fatal-marker 42"), "fatal.critical_persisted",
                         "契约：两目标阈值都为 Fatal 时 critical 消息仍落盘（fatal 不受裁剪影响）");
        ID::Bench::Check(ContainsText(log_content, "[critical]"), "fatal.level_name_critical",
                         "Level::Fatal 映射为 critical（文件行级别名）");
        ID::Bench::Check(!ContainsText(log_content, "trimmed-by-fatal-threshold"),
                         "fatal.lower_level_trimmed",
                         "同一子进程内 info 级被两目标的 Fatal 阈值裁剪，未落盘（证明阈值确实生效）");

        std::string child_stdout;
        const bool stdout_read = ReadWholeFile(child_stdout_path, child_stdout);
        ID::Bench::Check(stdout_read && ContainsText(child_stdout, "fatal-marker 42"),
                         "fatal.console_target_received",
                         "终端目标同样收到 critical 消息（flush 后进程才终止）");
#else
        ID::Bench::Uncovered("fatal.child_isolation",
                             "非 Windows 平台未实现子进程隔离（未声称 Linux 支持）：fatal 终止语义未取证");
#endif
    }

    // 契约（README §6）：通过已销毁句柄残留的 Logger* 继续写日志是违约用法；
    // 实现行为是不崩溃且该条消息被丢弃（契约不承诺安全，也不保证该行为稳定）
    void CaseStalePointerChild() noexcept
    {
#if defined(_WIN32)
        const std::string log_path = ID::Bench::TempPath("stale_child.log");
        const std::string child_stdout_path = ID::Bench::TempPath("stale_child_stdout.log");
        RemoveFileQuiet(log_path);
        RemoveFileQuiet(child_stdout_path);

        const ChildOutcome outcome = RunChildProcess("--stale-child=" + log_path, child_stdout_path);
        ID::Bench::Check(outcome.m_spawned, "stale.child_spawned",
                         "子进程创建成功（违约用法可能崩溃，必须跨进程取证）");
        if (!outcome.m_spawned)
        {
            return;
        }

        ID::Bench::Check(!outcome.m_timed_out, "stale.child_terminates",
                         "子进程在超时前正常结束（没有卡在失效对象上）");
        char exit_detail[192] = {};
        std::snprintf(exit_detail, sizeof(exit_detail),
                      "子进程退出码=0x%08lX；期望=0x00000000（实现行为：正常返回、不崩溃；"
                      "违约用法，契约不承诺安全）",
                      outcome.m_exit_code);
        ID::Bench::Check(outcome.m_exit_code == 0UL, "stale.child_no_crash", exit_detail);

        std::string log_content;
        const bool log_read = ReadWholeFile(log_path, log_content);
        ID::Bench::Check(log_read && ContainsText(log_content, "stale-before-destroy 1"),
                         "stale.before_destroy_written",
                         "违约前该句柄的文件目标正常写入（证明操作确实作用在一个可用日志器上）");
        ID::Bench::Check(log_read && !ContainsText(log_content, "stale-after-destroy"),
                         "stale.after_destroy_dropped",
                         "实现行为（违约用法，契约不承诺安全）：陈旧 Logger* 的写入被丢弃——"
                         "失效后写入的标记不出现在文件里");
#else
        ID::Bench::Uncovered("stale.pointer_child_isolation",
                             "非 Windows 平台未实现子进程隔离（未声称 Linux 支持）：违约不崩溃未取证");
#endif
    }

    // 未覆盖项登记：Skip 只表达"未选中/契约非目标"，这里登记的是有客观障碍、本轮确实没取证的项
    void CaseCoverageRegistration() noexcept
    {
        ID::Bench::Uncovered("write_failure.runtime_drop",
                             "未覆盖：运行期写失败无逐条返回值/状态可观测（契约只保证丢该条消息并向 stderr "
                             "限频报告），不改被测实现时没有稳定可控的注入手段，stderr 限频也无法与其它诊断区分");
        ID::Bench::Uncovered("init_shutdown.cost",
                             "未覆盖：init()/shutdown() 成本未列入指标（methodology §7 允许单独说明），"
                             "本轮只报告日志写入路径与登记路径的自身成本");
        ID::Bench::Uncovered("sink.config_after_shutdown",
                             "未覆盖：已取证的只有\"日志器已销毁\"这一违约用法（destroy_logger 之后槽位对象仍在池内，"
                             "实现行为是配置方法返回 false，见 sink.destroyed_config_methods_false）；"
                             "shutdown() 会结束槽位对象的生命周期，"
                             "此后用旧 Logger* 调用配置方法属于越界使用（UB），methodology §3 禁止在测试里触发 UB。"
                             "需要插件明确\"shutdown 后旧指针仍可安全调用\"这一承诺后才可取证");
    }
} // namespace

namespace ID::Bench
{
    void IdLogCorrectnessTest() noexcept
    {
        CaseUninitializedState();
        CaseInitAndRegistrationChain();
        CaseNullNameLogger();
        CaseShutdownCleanup();
        CaseFileOnlyVisibility();
        CaseConsoleOnlyVisibility();
        CaseBadPathKeepsOldTarget();
        CaseSamePathIdempotent();
        CaseFileReenableAppends();
        CaseSwitchPath();
        CaseEnableFileInvalidArgs();
        CaseFileEnableWithoutPath();
        CaseConsolePathRejected();
        CaseInvalidSinkType();
        CaseInvalidLevelRejected();
        CaseInvalidLevelWithInvalidSinkType();
        CaseReinitHandleAliasCurrentBehavior();
        CaseCreateShutdownRaceDefense();
        CaseDestroyedLoggerConfig();
        CaseConsoleLevelReuse();
        CaseFormatArgumentsAndLevelNames();
        CaseMultiLoggerConcurrency();
        CaseSingleLoggerConcurrency();
        CaseFatalTermination();
        CaseStalePointerChild();
        CaseCoverageRegistration();
    }

    int RunFatalChildCase(const char* log_path) noexcept
    {
        // 子进程：两个目标阈值都设为 Fatal，先写一条 info（应被裁剪），再 fatal（必须落盘并终止）
        ID::Log::init();

        const LoggerHandle handle = ID::Log::create_logger("fatal-child");
        Logger* logger = ID::Log::get_logger(handle);
        if (logger == nullptr || log_path == nullptr)
        {
            ID::Log::shutdown();
            return kFatalChildPrepFailed;
        }

        logger->set_sink_level(SinkType::Console, Level::Fatal);
        logger->set_sink_level(SinkType::File, Level::Fatal);
        if (!logger->set_sink_path(SinkType::File, log_path))
        {
            ID::Log::destroy_logger(handle);
            ID::Log::shutdown();
            return kFatalChildFileFailed;
        }

        logger->info("trimmed-by-fatal-threshold");
        logger->fatal("fatal-marker {}", 42);

        return kFatalChildNotTerminated;    // 未终止：父进程会把它判为非契约行为
    }

    int RunStalePointerChildCase(const char* log_path) noexcept
    {
        // 子进程：正常写一条 → destroy_logger（句柄与 Logger* 失效）→ 用陈旧指针再写两条 → 正常返回 0。
        // 实现行为是"不崩溃、消息被丢弃"（违约用法，契约不承诺安全），所以这里必须回到 main 并返回 0，
        // 不能靠异常或终止表达。
        ID::Log::init();

        const LoggerHandle handle = ID::Log::create_logger("stale-child");
        Logger* logger = ID::Log::get_logger(handle);
        if (logger == nullptr || log_path == nullptr)
        {
            ID::Log::shutdown();
            return kStaleChildPrepFailed;
        }

        logger->disable_sink(SinkType::Console);
        logger->set_sink_level(SinkType::File, Level::Trace);
        if (!logger->set_sink_path(SinkType::File, log_path))
        {
            ID::Log::destroy_logger(handle);
            ID::Log::shutdown();
            return kStaleChildFileFailed;
        }

        logger->info("stale-before-destroy {}", 1);
        ID::Log::destroy_logger(handle);            // 此后 handle 与 logger 均按契约失效

        logger->info("stale-after-destroy {}", 2);  // 违约用法：实现行为是不崩溃、消息被丢弃
        logger->warn("stale-after-destroy-warn");

        ID::Log::shutdown();
        return 0;
    }

    int RunDestroyedConfigChildCase() noexcept
    {
        // 子进程：destroy_logger 之后用陈旧 Logger* 调四个配置方法。实现行为是返回 false 且不崩溃
        // （违约用法，契约不承诺安全）；每个方法单独给一个返回码，父进程据此定位是哪一个方法没有返回 false。
        ID::Log::init();

        const LoggerHandle handle = ID::Log::create_logger("destroyed-config-child");
        Logger* logger = ID::Log::get_logger(handle);
        if (logger == nullptr)
        {
            ID::Log::shutdown();
            return kDestroyedChildPrepFailed;
        }

        logger->disable_sink(SinkType::Console);
        logger->set_sink_level(SinkType::File, Level::Trace);
        const std::string file_path = TempPath("destroyed_config_child.log");
        if (!logger->set_sink_path(SinkType::File, file_path.c_str()))
        {
            ID::Log::destroy_logger(handle);
            ID::Log::shutdown();
            return kDestroyedChildFileFailed;
        }

        ID::Log::destroy_logger(handle);        // 此后句柄与 logger 按契约失效（槽位对象仍在池内）

        if (logger->enable_sink(SinkType::Console))
        {
            ID::Log::shutdown();
            return kDestroyedChildEnableFailed;
        }
        if (logger->disable_sink(SinkType::File))
        {
            ID::Log::shutdown();
            return kDestroyedChildDisableFailed;
        }
        if (logger->set_sink_level(SinkType::Console, Level::Trace))
        {
            ID::Log::shutdown();
            return kDestroyedChildLevelFailed;
        }

        const std::string second_path = TempPath("destroyed_config_child2.log");
        if (logger->set_sink_path(SinkType::File, second_path.c_str()))
        {
            ID::Log::shutdown();
            return kDestroyedChildPathFailed;
        }

        ID::Log::shutdown();
        return 0;
    }
} // namespace ID::Bench
