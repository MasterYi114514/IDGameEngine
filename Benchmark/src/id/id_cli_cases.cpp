#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "benchmark_id.hpp"
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
 *  id.cli 子域用例：`ID cmake self --root <工程根> [--check]` 的用法、退出码、生成与 `--check` 幂等。
 *
 *  被测对象：构建期注入的 `BENCHMARK_ID_EXE`（`$<TARGET_FILE:ID>`，由构建清单建立依赖）指向的工具可执行文件。
 *  契约源：`docs/cmake-implementation-plan.md` §5.9（CLI 退出码 0/1/2/3、`<written|unchanged|failed:<类别>>
 *  <相对路径>` 逐行输出、四份生成文件清单、`--check` 只读且逐文件报类别）与 `skills/id-kernel` 第 10 节
 *  （脚本只按退出码判断成败）。
 *
 *  取证方式（framework.md 第 9 节：子进程隔离，判定依据是退出码集合 + 子进程实际落盘内容）：
 *  - Windows 用 `CreateProcessA` 启动被测工具，stdout 与 stderr 各重定向到一个独立捕获文件，
 *    运行后读回两份文本并记录退出码；超时（30 s）先终止再如实记录，不把超时当通过。
 *  - 每个用例自带沙箱：`<临时目录>/id/cli/<用例名>/root`（工程根，只建 `IDPlugin\{IDJson,IDLog,IDWindow}`
 *    三个空目录）与同级的 `capture/`；用例退出时整目录删除，不依赖清理顺序，也不触碰仓库里的真实文件。
 *  - 需要前置状态的用例自己再造一遍（先生成再 `--check`），不读其它用例留下的沙箱，因此顺序无关。
 *
 *  纪律：失败用 `Check` 记录并继续；非 Windows 平台没有子进程隔离，整段用例按 `Uncovered` 登记障碍
 *  （framework.md 第 9 节），不用 `Skip` 假装"本组合未选中"。
 */

#if defined(_WIN32)

namespace
{
    using ID::Bench::Check;
    using ID::Bench::TempPath;

    // §5.9 的退出码：0 成功、1 校验失败或 --check 不一致、2 写入失败、3 用法错误
    constexpr unsigned long kExitSuccess          = 0;
    constexpr unsigned long kExitValidationFailed = 1;
    constexpr unsigned long kExitUsage            = 3;

    // §5.9 的生成文件清单（相对 --root 的 POSIX 分隔路径，顺序即数据表顺序）
    constexpr std::string_view kSelfFiles[] = {
        "IDPlugin/CMakeLists.txt",
        "IDPlugin/IDJson/CMakeLists.txt",
        "IDPlugin/IDLog/CMakeLists.txt",
        "IDPlugin/IDWindow/CMakeLists.txt",
    };

    // 四份生成物的首行（§5.9：仓库内建模文件不写注释头，首行即 cmake_minimum_required）
    constexpr std::string_view kFirstLine = "cmake_minimum_required(VERSION 4.1.0)";

    // 单次调用的可观察结果：进程事实 + 两路捕获文本
    struct CliOutcome
    {
        bool          is_spawned   = false;   // 进程是否成功创建
        bool          is_timed_out = false;   // 是否超时后被终止
        unsigned long exit_code    = 0;       // 子进程退出码
        std::string   out_text     = {};       // 捕获到的 stdout（已去掉 '\r'）
        std::string   err_text     = {};       // 捕获到的 stderr（已去掉 '\r'）
    };

    std::string PathIn(const std::string& directory, std::string_view relative)
    {
        std::string path = directory;
        path.push_back('/');
        path.append(relative);
        return path;
    }

    // 整文件读取（二进制）；打不开时返回 false。捕获文件里的 CRLF 由调用方决定是否归一
    bool ReadWholeFile(const std::string& path, std::string& out, bool is_strip_cr) noexcept
    {
        out.clear();

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return false;
        }

        std::string bytes;
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if (stream.bad())
        {
            return false;
        }

        if (!is_strip_cr)
        {
            out = std::move(bytes);
            return true;
        }

        // 重定向到文件的 stdout 在 Windows CRT 文本模式下是 CRLF：比较状态行前统一去掉 '\r'
        out.reserve(bytes.size());
        for (const char unit : bytes)
        {
            if (unit != '\r')
            {
                out.push_back(unit);
            }
        }
        return true;
    }

    bool WriteWholeFile(const std::string& path, std::string_view bytes) noexcept
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        return stream.good();
    }

    // 沙箱：`<临时目录>/id/cli/<用例名>/root` 是工程根，同级 `capture/` 放子进程输出捕获文件
    class CliSandbox
    {
    public:
        explicit CliSandbox(std::string_view case_name)
            : m_directory(TempPath(std::string("id/cli/") + std::string(case_name)))
            , m_root(m_directory + "/root")
        {
            std::error_code error;
            std::filesystem::remove_all(std::filesystem::path(m_directory), error);
            std::filesystem::create_directories(std::filesystem::path(m_root + "/IDPlugin/IDJson"), error);
            std::filesystem::create_directories(std::filesystem::path(m_root + "/IDPlugin/IDLog"), error);
            std::filesystem::create_directories(std::filesystem::path(m_root + "/IDPlugin/IDWindow"), error);
            std::filesystem::create_directories(std::filesystem::path(m_directory + "/capture"), error);
        }

        ~CliSandbox()
        {
            std::error_code error;
            std::filesystem::remove_all(std::filesystem::path(m_directory), error);
        }

        CliSandbox(const CliSandbox&)            = delete;
        CliSandbox& operator=(const CliSandbox&) = delete;

        /// @brief 用例目录（子进程的工作目录；`root/` 与 `capture/` 都在它下面）
        const std::string& directory() const noexcept
        {
            return m_directory;
        }

        /// @brief 工程根（传给 `--root`）
        const std::string& root() const noexcept
        {
            return m_root;
        }

        /// @brief 捕获文件路径：`<用例目录>/capture/<tag>.<stream>.txt`
        std::string capture_path(std::string_view tag, std::string_view stream) const
        {
            return m_directory + "/capture/" + std::string(tag) + "." + std::string(stream) + ".txt";
        }

    private:
        std::string m_directory;
        std::string m_root;
    };

    /// @brief 一次调用的可核对描述（拼进 Check 的 detail，不做契约判定）
    std::string Describe(const CliOutcome& outcome)
    {
        return std::string("spawned=") + (outcome.is_spawned ? "true" : "false") + " timeout="
             + (outcome.is_timed_out ? "true" : "false") + " exit=" + std::to_string(outcome.exit_code)
             + " | stdout=[" + outcome.out_text + "] | stderr=[" + outcome.err_text + "]";
    }

    /// @brief 运行被测工具（子进程），捕获 stdout/stderr 与退出码
    CliOutcome RunTool(const CliSandbox& sandbox,
                       std::string_view  tag,
                       std::initializer_list<std::string_view> arguments) noexcept
    {
        CliOutcome outcome;

        const std::string stdout_path = sandbox.capture_path(tag, "stdout");
        const std::string stderr_path = sandbox.capture_path(tag, "stderr");

        SECURITY_ATTRIBUTES security{};
        security.nLength        = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE stdout_handle = ::CreateFileA(stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stdout_handle == INVALID_HANDLE_VALUE)
        {
            return outcome;
        }

        HANDLE stderr_handle = ::CreateFileA(stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stderr_handle == INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(stdout_handle);
            return outcome;
        }

        STARTUPINFOA startup{};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdOutput = stdout_handle;
        startup.hStdError  = stderr_handle;
        startup.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

        // 工具路径与每个参数都加引号：路径含空格时不被二次分词；工作目录固定为用例目录，
        // 使缺省 --root（当前工作目录）与显式 --root 的用例都不受本进程工作目录影响
        std::string command_line = std::string("\"") + BENCHMARK_ID_EXE + "\"";
        for (const std::string_view argument : arguments)
        {
            command_line += " \"";
            command_line.append(argument);
            command_line += "\"";
        }

        PROCESS_INFORMATION process{};
        std::fflush(stdout);
        const BOOL created = ::CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                             sandbox.directory().c_str(), &startup, &process);
        ::CloseHandle(stdout_handle);
        ::CloseHandle(stderr_handle);
        if (created == FALSE)
        {
            return outcome;
        }
        outcome.is_spawned = true;

        if (::WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            if (::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE)
            {
                outcome.exit_code = static_cast<unsigned long>(exit_code);
            }
        }
        else
        {
            // 超时不是通过：先终止再等回收，超时事实写进 detail
            outcome.is_timed_out = true;
            ::TerminateProcess(process.hProcess, 1);
            ::WaitForSingleObject(process.hProcess, 5000);
        }

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);

        ReadWholeFile(stdout_path, outcome.out_text, true);
        ReadWholeFile(stderr_path, outcome.err_text, true);
        return outcome;
    }

    /// @brief 输出里是否存在恰好等于 `<状态> <相对路径>` 的一行（§5.9 的逐行输出格式）
    bool HasStatusLine(std::string_view text, std::string_view status, std::string_view relative_path) noexcept
    {
        const std::string wanted = std::string(status) + " " + std::string(relative_path);

        std::size_t begin = 0;
        while (begin <= text.size())
        {
            const std::size_t end  = text.find('\n', begin);
            const std::size_t stop = (end == std::string_view::npos) ? text.size() : end;
            if (text.substr(begin, stop - begin) == wanted)
            {
                return true;
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            begin = end + 1;
        }
        return false;
    }

    /// @brief 目录下（递归）普通文件数：用于证明 `--check` 没有写盘
    std::size_t CountRegularFiles(const std::string& directory) noexcept
    {
        std::error_code error;
        std::size_t     count = 0;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(std::filesystem::path(directory), error))
        {
            if (entry.is_regular_file(error))
            {
                ++count;
            }
        }
        return count;
    }

    // ==================== 用法与退出码（§5.9：用法错误一律 3） ====================

    void CaseUsageNoArgs() noexcept
    {
        const CliSandbox sandbox("usage_no_args");
        const CliOutcome outcome = RunTool(sandbox, "no_args", {});

        Check(outcome.is_spawned && !outcome.is_timed_out && outcome.exit_code == kExitUsage
                  && !outcome.err_text.empty(),
              "id.cli.usage.no_args",
              "无参数 → 退出码 3，且用法文本写到 stderr（stdout 不承载用法）；" + Describe(outcome));
    }

    void CaseUsageUnknownSubcommand() noexcept
    {
        const CliSandbox sandbox("usage_unknown_subcommand");
        const CliOutcome outcome = RunTool(sandbox, "unknown_subcommand", {"foo"});

        // 措辞级附加信息：stderr 要能指出出错的对象（参数原文）；契约判定是退出码 3
        Check(outcome.is_spawned && !outcome.is_timed_out && outcome.exit_code == kExitUsage
                  && outcome.err_text.find("foo") != std::string::npos,
              "id.cli.usage.unknown_subcommand",
              "未知子命令 foo → 退出码 3，stderr 指出来错的参数原文；" + Describe(outcome));
    }

    void CaseUsageMissingRootValue() noexcept
    {
        const CliSandbox sandbox("usage_missing_root_value");
        const CliOutcome outcome = RunTool(sandbox, "missing_root_value", {"cmake", "self", "--root"});

        // 缺取值属用法错误（不是"用当前工作目录代替"）：退出码 3
        Check(outcome.is_spawned && !outcome.is_timed_out && outcome.exit_code == kExitUsage
                  && outcome.err_text.find("--root") != std::string::npos,
              "id.cli.usage.missing_root_value",
              "`cmake self --root` 缺取值 → 退出码 3，stderr 指出缺的是 --root 的取值；" + Describe(outcome));
    }

    // ==================== 生成与 --check 幂等（§5.9） ====================

    void CaseSelfCheckMissing() noexcept
    {
        const CliSandbox sandbox("self_check_missing");
        const CliOutcome outcome =
            RunTool(sandbox, "check_missing", {"cmake", "self", "--root", sandbox.root(), "--check"});

        bool is_ok = outcome.is_spawned && !outcome.is_timed_out && outcome.exit_code == kExitValidationFailed;
        for (const std::string_view file : kSelfFiles)
        {
            // 缺失按校验失败计（failed:MissingFile），不按写入失败计 2
            if (!HasStatusLine(outcome.out_text, "failed:MissingFile", file))
            {
                is_ok = false;
            }
        }

        // --check 只读：不构造 FragmentWriter、不写盘，目录内一个普通文件都不该出现
        const std::size_t file_count = CountRegularFiles(sandbox.root());
        Check(is_ok && file_count == 0,
              "id.cli.self.check_missing",
              "空工程根 + --check → 退出码 1、四行 failed:MissingFile，工程根内普通文件数 0（实际 " +
                  std::to_string(file_count) + "）；" + Describe(outcome));
    }

    void CaseSelfGenerate() noexcept
    {
        const CliSandbox sandbox("self_generate");
        const CliOutcome outcome = RunTool(sandbox, "generate", {"cmake", "self", "--root", sandbox.root()});

        bool is_ok = outcome.is_spawned && !outcome.is_timed_out && outcome.exit_code == kExitSuccess;
        for (const std::string_view file : kSelfFiles)
        {
            if (!HasStatusLine(outcome.out_text, "written", file))
            {
                is_ok = false;
            }

            std::string content;
            if (!ReadWholeFile(PathIn(sandbox.root(), file), content, false) || content.empty())
            {
                is_ok = false;
                continue;
            }

            // 产物形态：首行即 cmake_minimum_required（不写注释头），见 §5.9 与 §5.6
            if (!content.starts_with(kFirstLine))
            {
                is_ok = false;
            }
        }

        Check(is_ok, "id.cli.self.generate",
              "生成 → 退出码 0、四行 written、四份文件存在且非空且首行为 cmake_minimum_required；" +
                  Describe(outcome));
    }

    void CaseSelfCheckClean() noexcept
    {
        const CliSandbox sandbox("self_check_clean");

        // 前置：先用同一沙箱生成一次（用例自包含，不吃别的用例留下的状态）
        const CliOutcome generated = RunTool(sandbox, "generate", {"cmake", "self", "--root", sandbox.root()});
        const CliOutcome outcome =
            RunTool(sandbox, "check", {"cmake", "self", "--root", sandbox.root(), "--check"});

        bool is_ok = generated.is_spawned && !generated.is_timed_out && generated.exit_code == kExitSuccess
                  && outcome.is_spawned && !outcome.is_timed_out && outcome.exit_code == kExitSuccess;
        for (const std::string_view file : kSelfFiles)
        {
            if (!HasStatusLine(outcome.out_text, "unchanged", file))
            {
                is_ok = false;
            }
        }

        Check(is_ok, "id.cli.self.check_clean",
              "生成后 --check → 退出码 0 且四行 unchanged（前置生成 exit=" +
                  std::to_string(generated.exit_code) + "）；" + Describe(outcome));
    }

    void CaseSelfRegenerateUnchanged() noexcept
    {
        const CliSandbox sandbox("self_regenerate_unchanged");

        const CliOutcome first  = RunTool(sandbox, "generate_first", {"cmake", "self", "--root", sandbox.root()});
        const CliOutcome second = RunTool(sandbox, "generate_second", {"cmake", "self", "--root", sandbox.root()});

        bool is_ok = first.is_spawned && !first.is_timed_out && first.exit_code == kExitSuccess
                  && second.is_spawned && !second.is_timed_out && second.exit_code == kExitSuccess;
        for (const std::string_view file : kSelfFiles)
        {
            // 内容未变时 writer 幂等跳过，状态行因此是 unchanged 而不是 written
            if (!HasStatusLine(second.out_text, "unchanged", file))
            {
                is_ok = false;
            }
        }

        Check(is_ok, "id.cli.self.regenerate_unchanged",
              "再生成一次 → 退出码 0 且四行 unchanged（前置生成 exit=" + std::to_string(first.exit_code) + "）；" +
                  Describe(second));
    }

    void CaseSelfCheckAfterEdit() noexcept
    {
        const CliSandbox sandbox("self_check_after_edit");
        constexpr std::string_view kEditedFile = "IDPlugin/IDJson/CMakeLists.txt";

        const CliOutcome generated = RunTool(sandbox, "generate", {"cmake", "self", "--root", sandbox.root()});

        // 前置：生成成功后改动其中一份文件（末尾追加一行注释），模拟用户手工改动
        const std::string path = PathIn(sandbox.root(), kEditedFile);
        std::string       edited;
        bool is_prepared = generated.is_spawned && !generated.is_timed_out
                        && generated.exit_code == kExitSuccess
                        && ReadWholeFile(path, edited, false);
        if (is_prepared)
        {
            edited += "# T51 手工改动：--check 必须报 TargetModified\n";
            is_prepared = WriteWholeFile(path, edited);
        }

        const CliOutcome outcome = is_prepared
                                       ? RunTool(sandbox, "check_after_edit",
                                                 {"cmake", "self", "--root", sandbox.root(), "--check"})
                                       : CliOutcome{};

        bool is_ok = is_prepared && outcome.is_spawned && !outcome.is_timed_out
                  && outcome.exit_code == kExitValidationFailed
                  && HasStatusLine(outcome.out_text, "failed:TargetModified", kEditedFile);
        for (const std::string_view file : kSelfFiles)
        {
            if (file != kEditedFile && !HasStatusLine(outcome.out_text, "unchanged", file))
            {
                is_ok = false;
            }
        }

        // --check 只读：改动必须仍在（不能被回写覆盖）
        std::string after;
        if (!ReadWholeFile(path, after, false) || after != edited)
        {
            is_ok = false;
        }

        Check(is_ok, "id.cli.self.check_after_edit",
              "改动一份文件后 --check → 退出码 1、该行 failed:TargetModified、其余三行 unchanged，"
              "且文件内容未被回写；" + Describe(outcome));
    }
} // namespace

#endif // defined(_WIN32)

namespace ID::Bench
{
    void IdCliCorrectnessTest() noexcept
    {
        ReportLine("config (id.cli): 被测对象 = 构建期注入的 BENCHMARK_ID_EXE 指向的 ID 工具；调用方式 = 子进程执行 "
                   "`ID cmake self --root <工程根> [--check]`，逐用例捕获 stdout/stderr 与退出码");
        ReportLine("config (id.cli): 判定口径 = 退出码（0 成功 / 1 校验失败或 --check 不一致 / 2 写入失败 / 3 用法错误）"
                   "+ 逐行 `<状态> <相对路径>` 输出 + 落盘文件是否存在与内容；沙箱按用例独立建在 BENCHMARK_TEMP_DIR 下");
        ReportLine("config (id.cli): 契约源 = docs/cmake-implementation-plan.md §5.9 与 skills/id-kernel 第 10 节"
                   "（脚本只按退出码判断成败）");

#if !defined(_WIN32)
        // 非 Windows 平台没有子进程隔离（framework.md 第 9 节）：不用 Skip 假装"本组合未选中"，按 Uncovered 登记
        ReportLine("config (id.cli): 本平台无子进程隔离，八条用例整体登记为 Uncovered");
        Uncovered("id.cli.usage.no_args", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节），"
                                          "无法受控调用被测工具并捕获其退出码与两路输出");
        Uncovered("id.cli.usage.unknown_subcommand", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        Uncovered("id.cli.usage.missing_root_value", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        Uncovered("id.cli.self.check_missing", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        Uncovered("id.cli.self.generate", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        Uncovered("id.cli.self.check_clean", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        Uncovered("id.cli.self.regenerate_unchanged", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        Uncovered("id.cli.self.check_after_edit", "非 Windows 平台尚未实现子进程隔离（framework.md 第 9 节）");
        return;
#else
        if (std::string_view(BENCHMARK_ID_EXE).empty())
        {
            // 构建未注入工具路径时不猜路径：整段用例按"无法受控取证"登记，不伪造通过
            Uncovered("id.cli.all", "构建未注入 BENCHMARK_ID_EXE（应为 $<TARGET_FILE:ID>），无法定位被测工具");
            return;
        }

        CaseUsageNoArgs();
        CaseUsageUnknownSubcommand();
        CaseUsageMissingRootValue();
        CaseSelfCheckMissing();
        CaseSelfGenerate();
        CaseSelfCheckClean();
        CaseSelfRegenerateUnchanged();
        CaseSelfCheckAfterEdit();
#endif
    }
} // namespace ID::Bench
