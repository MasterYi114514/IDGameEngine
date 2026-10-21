#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "IDGameEngine.hpp"
#include "IDLogLogger.hpp"

/**
 *  ID.Log 终止语义校验（Benchmark/validation/log，默认 OFF 的校验资产）
 *  1. 本文件只做官方进程内**不能**做的验证：`Fatal` 必终止、shutdown 之后使用旧句柄的误用后果。
 *     官方 Benchmark 对这些路径只 Skip——它们会 abort/崩溃，不能与其它用例共享进程；
 *  2. 同一个 exe 的父/子两种角色：父进程用**带引号**的自身路径 + `2> stderr文件` 启动子进程，
 *     判定同时要求三件事，缺一即失败：
 *     a. 退出码落在该用例的点名允许集合内（不是"非 0 即可"）；
 *     b. 该用例的**逐条点名文本**出现在 stderr 里（证明"先输出后终止"，而不是先崩后没输出）；
 *     c. 旧句柄用例额外要求 `STALE_HANDLE_BEFORE_DEREF`：子进程在解引用旧句柄**之前**写出它，
 *        因此"marker 出现 + 访问违例退出码"能区分"崩在解引用处"与"崩在别处"；
 *  3. 允许集合来自实测，不靠猜：MinGW GCC 16.2.0 与 MSVC 19.52 各跑一遍后取并集，见 `kCases` 注释；
 *     同一条用例在 Debug / Release 下也可能给出不同的 abort 码（MSVC Debug CRT 的 abort 走 `_exit(3)`，
 *     Release CRT 才触发 fast-fail），所以每个用例给的是**该用例实测到的码的集合**，不是"任意非 0"；
 *  4. 判定逻辑自带自检：合成输入（退出码 0 / 缺 marker / 退出码不在集合内）必须被判为失败，
 *     防止驱动器退化成"只看退出码非 0"或"只看 marker"；
 *  5. 输出只写本构建目录：`validation-summary.txt` 与 `stderr-<case>.log`；
 *     **不写** `Benchmark/report.txt`、不进官方 exe、不参与六档评级。
 */

// 输出目录由 CMake 以编译期宏注入（当前构建目录），保证与运行时 CWD 无关
#ifndef ID_LOG_VALIDATION_DIR
    #define ID_LOG_VALIDATION_DIR "."
#endif

namespace
{
    constexpr const char* kOutDir = ID_LOG_VALIDATION_DIR;

    /**
     *  @brief 一个子进程用例
     */
    struct CaseSpec
    {
        const char* name;              // 子进程 argv[1]
        const char* marker;            // 必须出现在子进程 stderr 里的文本
        const char* exit_rule;         // 退出码判据的人类可读说明（写进汇总，便于核对）
        unsigned    allowed_exit[3];   // 允许的退出码集合（0 结尾）；空集合 = 只要非 0
        const char* description;       // 父进程汇总里的一句话说明
    };

    // marker 与用例名一一对应：父进程按"逐条点名"判定，不做行数/次数统计。
    // 允许集合来自实测（不是推测），逐条列出该用例实测到的全部终止码：
    //   MinGW GCC 16.2.0（Debug / Release 实测相同）：logger_fatal / macro_fatal_null / null_table_logger
    //       = 0xC0000409（CRT fast-fail），write_fatal_after_shutdown = 0xC000001D（断言陷阱），
    //       旧句柄 = 0xC0000005（空指针解引用）；
    //   MSVC 19.52 Debug：前三者 = 0x00000003（Debug CRT 的 abort 直接 `_exit(3)`，不走 fast-fail），
    //       write_fatal_after_shutdown = 0x80000003（__debugbreak），旧句柄 = 0xC0000005；
    //   MSVC 19.52 Release：前三者回到 0xC0000409（Release CRT 的 abort 触发 fast-fail），
    //       后两条与 Debug 相同。
    // 因此前三条的允许集合是 Debug / Release 的并集；"3" 只对这三条 abort 用例成立，
    // write_fatal_after_shutdown 与旧句柄仍只接受各自的码。新增工具链若给出别的终止码，
    // 必须实测后写进这里，不允许退化成"非 0 即可"。
    constexpr CaseSpec kCases[] =
    {
        { "logger_fatal", "VALIDATION_MARKER_logger_fatal",
          "0xC0000409 fast-fail（std::abort 路径）或 0x00000003（MSVC Debug CRT abort 退出码）",
          { 0xC0000409u, 0x00000003u },
          "Ready 后端上 logger->fatal()：同步输出 + flush 后 abort" },
        { "macro_fatal_null", "VALIDATION_MARKER_macro_fatal_null",
          "0xC0000409 fast-fail（std::abort 路径）或 0x00000003（MSVC Debug CRT abort 退出码）",
          { 0xC0000409u, 0x00000003u },
          "ID_LOG_FATAL(nullptr, ...)：经 BootstrapLog 回退输出后终止" },
        { "null_table_logger", "VALIDATION_MARKER_null_table_logger",
          "0xC0000409 fast-fail（std::abort 路径）或 0x00000003（MSVC Debug CRT abort 退出码）",
          { 0xC0000409u, 0x00000003u },
          "句柄 table==nullptr（替代表形态）：fatal() 走回退路径，仍然先输出后终止" },
        { "write_fatal_after_shutdown", "VALIDATION_MARKER_write_fatal_after_shutdown",
          "0xC000001D（MinGW 断言陷阱）或 0x80000003（MSVC __debugbreak）", { 0xC000001Du, 0x80000003u },
          "shutdown 之后 table->write(Fatal)：后端不可用也必须输出后终止" },
        // 旧句柄：必须先看到解引用前的点名文本，再看到空指针解引用的访问违例退出码。
        // 只判"非 0"会把"崩在任何别的地方"也算通过；只判 marker 会把"输出了但没崩"也算通过。
        { "stale_handle_after_shutdown", "STALE_HANDLE_BEFORE_DEREF",
          "0xC0000005 空指针解引用（access violation）", { 0xC0000005u },
          "shutdown 之后解引用旧句柄（契约 UB）：解引用前写 marker，随后必须是访问违例" },
    };

    constexpr const char* kSummaryName = "validation-summary.txt";

    /**
     *  @brief 判断退出码是否落在用例声明的允许集合内
     *  @param exit_code `std::system` 返回的退出码（可能为负：Windows 把异常码按 int 返回）
     *  @param spec 用例声明
     *  @return 允许集合为空时要求非 0；否则要求等于集合中的某一项
     *  @note 比较用无符号转换：0xC0000005 之类的异常码在 `int` 里是负数，直接比大小会漏判
     */
    bool exit_code_allowed(int exit_code, const CaseSpec& spec) noexcept
    {
        if (spec.allowed_exit[0] == 0u)
        {
            return exit_code != 0;
        }

        const unsigned code = static_cast<unsigned>(exit_code);
        for (int index = 0; index < 3 && spec.allowed_exit[index] != 0u; ++index)
        {
            if (code == spec.allowed_exit[index])
            {
                return true;
            }
        }
        return false;
    }

    /**
     *  @brief 判定一次子进程运行是否满足"按点名方式终止且先输出"
     *  @param exit_code 子进程退出码；0 一律不算通过
     *  @param stderr_text 子进程 stderr 的全部内容
     *  @param spec 用例声明（marker + 允许的退出码集合）
     *  @return 退出码在允许集合内且 marker 命中时为 true
     *  @note 判定不做模糊匹配、不看行数：要么出现点名的完整文本，要么失败
     */
    bool judge(int exit_code, const std::string& stderr_text, const CaseSpec& spec) noexcept
    {
        if (exit_code == 0)
        {
            return false;
        }
        if (!exit_code_allowed(exit_code, spec))
        {
            return false;
        }
        if (spec.marker == nullptr || spec.marker[0] == '\0')
        {
            return true;
        }
        return stderr_text.find(spec.marker) != std::string::npos;
    }

    /**
     *  @brief 读一个文件的全部内容
     *  @param path 文件路径
     *  @param out 输出
     *  @return 文件能打开时为 true（空文件也算成功）
     *  @note 用 C 文件 API：`std::ifstream` 在 MSVC 的 `/EHs-c-` 下会触发 C4530，
     *        而本工程不使用异常
     */
    bool read_file(const std::string& path, std::string* out) noexcept
    {
        std::FILE* const file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }

        char chunk[512];
        for (;;)
        {
            const std::size_t read_bytes = std::fread(chunk, 1, sizeof(chunk), file);
            if (read_bytes == 0)
            {
                break;
            }
            out->append(chunk, read_bytes);
        }
        std::fclose(file);
        return true;
    }

    /**
     *  @brief 把 CMake 注入的 POSIX 风格路径转成 cmd.exe 更稳妥的反斜杠形式
     *  @param path 原始路径（可能含 '/'）
     *  @return 把 '/' 全部换成 '\\' 的副本
     *  @note 只用于命令行与重定向目标；读文件仍用原始路径（两者在同一台 Windows 上等价）
     */
    std::string to_command_path(const std::string& path) noexcept
    {
        std::string result = path;
        for (char& ch : result)
        {
            if (ch == '/')
            {
                ch = '\\';
            }
        }
        return result;
    }

    /**
     *  @brief 把一行同时写到 stdout 与汇总文件
     *  @param summary 汇总文件；允许为空（打不开时只写 console）
     *  @param text 已经格式化好的整行（含换行）
     */
    void emit(std::FILE* summary, const char* text) noexcept
    {
        std::fputs(text, stdout);
        if (summary != nullptr)
        {
            std::fputs(text, summary);
        }
    }

    /**
     *  @brief 父进程侧：驱动器判定逻辑的自检
     *  @param summary 汇总文件
     *  @return 五条自检全部成立时为 true
     *  @note 证明驱动器会拒绝"退出码 0"、"缺 marker"、"退出码不在允许集合内"三种错误输出，
     *        而不是只数退出码或只看 marker；另证明 MSVC Debug 的 abort 码只对点名它的用例成立
     */
    bool run_self_checks(std::FILE* summary) noexcept
    {
        constexpr CaseSpec kProbe{ "self_check", "SELF_CHECK_MARKER", "0xC0000005", { 0xC0000005u }, "" };
        const int          access_violation = static_cast<int>(0xC0000005u);
        const int          debug_crt_abort  = static_cast<int>(0x00000003u);

        const bool reject_zero_exit           = !judge(0, kProbe.marker, kProbe);
        const bool reject_no_marker           = !judge(access_violation, "unrelated stderr text", kProbe);
        const bool reject_wrong_exit          = !judge(7, kProbe.marker, kProbe);      // 常见误判：只判非 0
        const bool accept_marker_and_code     = judge(access_violation,
                                                     "prefix SELF_CHECK_MARKER suffix", kProbe);
        // 0x00000003 只属于 kCases 里的三条 abort 用例：同一码配旧句柄用例的 marker 必须被拒，
        // 否则等于把"允许集合"放宽成一张坏码白名单
        const bool debug_abort_is_case_scoped = judge(debug_crt_abort, kCases[0].marker, kCases[0])
                                             && !judge(debug_crt_abort, kCases[4].marker, kCases[4]);

        emit(summary, reject_zero_exit ? "selfcheck.judge_rejects_zero_exit      : PASS\n"
                                       : "selfcheck.judge_rejects_zero_exit      : FAIL\n");
        emit(summary, reject_no_marker ? "selfcheck.judge_rejects_missing_marker : PASS\n"
                                       : "selfcheck.judge_rejects_missing_marker : FAIL\n");
        emit(summary, reject_wrong_exit ? "selfcheck.judge_rejects_wrong_exit_code: PASS\n"
                                        : "selfcheck.judge_rejects_wrong_exit_code: FAIL\n");
        emit(summary, accept_marker_and_code ? "selfcheck.judge_accepts_marker_and_code: PASS\n"
                                             : "selfcheck.judge_accepts_marker_and_code: FAIL\n");
        emit(summary, debug_abort_is_case_scoped
                          ? "selfcheck.debug_abort_code_is_case_scoped: PASS\n"
                          : "selfcheck.debug_abort_code_is_case_scoped: FAIL\n");
        return reject_zero_exit && reject_no_marker && reject_wrong_exit && accept_marker_and_code
            && debug_abort_is_case_scoped;
    }

    /**
     *  @brief 子进程：完成用例并触发终止
     *  @param case_name argv[1]，必须是 `kCases` 里的名字
     *  @return 只有"没有终止"的路径才会返回；返回 0 表示该用例未按预期终止
     */
    int run_child(const std::string& case_name) noexcept
    {
        const ID::PluginRuntimeDiagnostic started = ID::Engine::start();
        if (!started.is_ok())
        {
            std::fprintf(stderr, "[child] Engine::start() failed: code=%u\n",
                         static_cast<unsigned>(static_cast<ID::uint32_t>(started.code)));
            return 2;
        }

        const ID::Log::LogApiTable* const table =
            ID::PluginRuntime::instance().get_service<ID::Log::LogApiTable>(ID::ServiceSlot::Log);
        if (table == nullptr)
        {
            std::fprintf(stderr, "[child] ServiceSlot::Log is empty after start\n");
            return 3;
        }

        ID::Log::Logger* const logger = ID::Log::acquire_logger(*table, "Bench.Validation");
        if (logger == nullptr)
        {
            std::fprintf(stderr, "[child] acquire_logger returned nullptr\n");
            return 4;
        }

        std::printf("[child] case=%s ready; triggering termination\n", case_name.c_str());
        std::fflush(stdout);

        if (case_name == "logger_fatal")
        {
            logger->fatal("VALIDATION_MARKER_logger_fatal");
        }

        if (case_name == "macro_fatal_null")
        {
            ID_LOG_FATAL(nullptr, "VALIDATION_MARKER_macro_fatal_null");
        }

        if (case_name == "null_table_logger")
        {
            // 替代表形态：句柄存在但 table 为空（shutdown 之后被复位的旧句柄就是这种形状）
            const ID::Log::Logger detached{};
            detached.fatal("VALIDATION_MARKER_null_table_logger");
        }

        if (case_name == "write_fatal_after_shutdown")
        {
            const ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
            if (!stopped.is_ok())
            {
                std::fprintf(stderr, "[child] shutdown failed before write_fatal_after_shutdown\n");
                return 5;
            }

            // 服务表本体是 DLL 内静态存储期对象，shutdown 之后指针仍有效；
            // 但后端已销毁，Fatal 必须转交 BootstrapLog 输出后终止
            constexpr char kCategory[] = "Bench.Validation";
            constexpr char kMessage[]  = "VALIDATION_MARKER_write_fatal_after_shutdown";
            table->write(static_cast<ID::uint32_t>(ID::Log::Level::Fatal),
                         kCategory, static_cast<ID::uint32_t>(sizeof(kCategory) - 1),
                         kMessage, static_cast<ID::uint32_t>(sizeof(kMessage) - 1));
        }

        if (case_name == "stale_handle_after_shutdown")
        {
            const ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
            if (!stopped.is_ok())
            {
                std::fprintf(stderr, "[child] shutdown failed before stale_handle_after_shutdown\n");
                return 5;
            }

            std::printf("[child] case=%s shutdown done; touching stale handle (contract UB)\n",
                        case_name.c_str());
            std::fflush(stdout);

            // 解引用之前先把点名文本写到 stderr 并 flush：父进程据此证明"进程是在解引用这一刻崩的"，
            // 而不是崩在启动/关闭等无关位置。marker 必须在解引用之前，事后没有机会再写。
            std::fprintf(stderr, "STALE_HANDLE_BEFORE_DEREF: old handle deref about to happen (contract UB)\n");
            std::fflush(stderr);

            // 契约明确：句柄只在 Ready 区间有效，on_shutdown 复位后 table 为 nullptr。
            // 这里故意解引用，验证"误用后果确实是空指针崩溃"而不是静默成功
            logger->info("stale handle probe after shutdown");

            std::printf("[child] stale handle did NOT terminate the process\n");
            std::fflush(stdout);
            return 0;
        }

        std::fprintf(stderr, "[child] unknown case: %s\n", case_name.c_str());
        return 6;
    }

    /**
     *  @brief 父进程：逐个用例起子进程、按"退出码 + 点名文本"判定
     *  @param self 自身可执行文件路径（argv[0]）
     *  @return 全部用例通过时为 0，否则 1
     */
    int run_parent(const char* self) noexcept
    {
        const std::string summary_path = std::string(kOutDir) + "/" + kSummaryName;
        std::FILE* const summary = std::fopen(summary_path.c_str(), "w");
        if (summary == nullptr)
        {
            std::printf("[WARN] cannot open %s, console output is still valid\n", summary_path.c_str());
        }

        char line[1024];
        std::snprintf(line, sizeof(line), "=== IDLog termination validation (parent/child isolation) ===\n");
        emit(summary, line);
        std::snprintf(line, sizeof(line), "self        : %s\n", self);
        emit(summary, line);
        std::snprintf(line, sizeof(line), "output dir  : %s\n", kOutDir);
        emit(summary, line);
        std::snprintf(line, sizeof(line), "cases       : %zu\n", sizeof(kCases) / sizeof(kCases[0]));
        emit(summary, line);
        emit(summary, "\n");

        int failed = run_self_checks(summary) ? 0 : 1;
        emit(summary, "\n");

        const int case_count = static_cast<int>(sizeof(kCases) / sizeof(kCases[0]));
        int passed = 0;

        for (int index = 0; index < case_count; ++index)
        {
            const CaseSpec& spec = kCases[index];
            const std::string stderr_path =
                std::string(kOutDir) + "/stderr-" + spec.name + ".log";

            std::remove(stderr_path.c_str());   // 清掉上一轮同名文件：读到的必然是本次输出

            // cmd.exe 的引号规则：整串以引号开头且以引号结尾时会去掉首尾各一个引号，
            // 所以这里再套一层引号，实际执行的是 ""<exe>" <case> 2> "<stderr文件>""
            const std::string exe_path = to_command_path(self);
            const std::string redirect = to_command_path(stderr_path);
            char command[2048];
            std::snprintf(command, sizeof(command), "\"\"%s\" %s 2> \"%s\"\"",
                          exe_path.c_str(), spec.name, redirect.c_str());
            const int exit_code = std::system(command);

            std::string stderr_text;
            const bool captured = read_file(stderr_path, &stderr_text);
            const bool ok = captured && judge(exit_code, stderr_text, spec);

            std::snprintf(line, sizeof(line), "case=%-30s exit=%-12d (0x%08X) stderr=%-6zu -> %s\n",
                          spec.name, exit_code, static_cast<unsigned>(exit_code),
                          stderr_text.size(), ok ? "PASS" : "FAIL");
            emit(summary, line);
            std::snprintf(line, sizeof(line), "    %s\n", spec.description);
            emit(summary, line);
            std::snprintf(line, sizeof(line), "    exit rule : %s\n", spec.exit_rule);
            emit(summary, line);
            std::snprintf(line, sizeof(line), "    marker    : %s\n", spec.marker);
            emit(summary, line);

            if (ok)
            {
                ++passed;
            }
            else
            {
                ++failed;
                if (!captured)
                {
                    emit(summary, "    reason: 子进程 stderr 文件不存在或打不开（命令未能执行？）\n");
                }
                else if (exit_code == 0)
                {
                    emit(summary, "    reason: 子进程正常退出，Fatal/误用路径必须终止进程\n");
                }
                else if (!exit_code_allowed(exit_code, spec))
                {
                    std::snprintf(line, sizeof(line),
                                  "    reason: 退出码 0x%08X 不在允许集合内（判据见 exit rule），"
                                  "不能只凭\"非 0\"算通过\n",
                                  static_cast<unsigned>(exit_code));
                    emit(summary, line);
                }
                else
                {
                    emit(summary, "    reason: stderr 里没有该用例的点名文本（先输出后终止的判据不成立）\n");
                }
            }
        }

        std::snprintf(line, sizeof(line), "\nresult: %d passed / %d failed\n", passed, failed);
        emit(summary, line);

        if (summary != nullptr)
        {
            std::fflush(summary);
            std::fclose(summary);
        }
        std::fflush(stdout);
        return failed == 0 ? 0 : 1;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        return run_child(std::string(argv[1]));
    }
    return run_parent(argv[0]);
}
