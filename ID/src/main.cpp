#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// 平台头：Windows 必须从宽字符命令行取参（CRT 的 argv 按 ANSI 代码页解释，非 ASCII 路径会指错目录）
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>

    #include <shellapi.h>
#endif

#include "cmake/Common/Diagnostic.hpp"
#include "cmake/File/FragmentWriter.hpp"
#include "generate/SelfCmake.hpp"
#include "project/Base/Inject.hpp"
#include "project/Cli/Runner.hpp"

/**
 *  `ID` 的命令行入口：只做参数解析、按固定顺序调用生成层与落盘层、把结果映射成退出码。
 *
 *  当前唯一的命令族是 `ID cmake self [--root <工程根>] [--check]`：按 `generate/SelfCmake` 的数据表
 *  产出 `IDPlugin` 下四份 `CMakeLists.txt`。顺序固定为「构造模型 → validate → 逐文件比较或落盘」，
 *  校验失败时不产出文本、不写任何文件（CMake 封装计划书 §5.9）。
 *
 *  退出码是脚本判断成败的唯一依据（`id-kernel` 第 10 节）：0 成功、1 校验失败或 `--check` 有不一致、
 *  2 写入失败、3 用法错误（无参数、未知子命令、未知参数、缺取值、`--root` 指向不存在的目录）。
 *  失败不伪装成功，也不靠输出文本判断成败。
 *
 *  输出：每个目标文件一行 `<状态> <相对路径>`（状态取 written / unchanged / failed:<类别>），
 *  失败时在该行之后逐条向 stderr 打印 `Diagnostic` 的六个字段。
 *
 *  `--check` 只读：不构造 `FragmentWriter`、不写盘；文件缺失报 `MissingFile`、内容与模型产出不一致报
 *  `TargetModified`，两者都按校验失败计退出码 1，不按写入失败处理。
 *
 *  参数编码：命令行参数在本文件内统一按 UTF-8 承载。Windows 下 CRT 的 `argv` 按 ANSI 代码页解释，
 *  非 ASCII 的 `--root` 会指向错误目录，因此参数不取自 `argv`，而由 `GetCommandLineW` +
 *  `CommandLineToArgvW` 取宽字符原文后逐项转 UTF-8；其它平台的 `argv` 本身就是 UTF-8，直接使用。
 */

namespace
{
    // 退出码取值固定在本文件：脚本与 CI 按它判断，日志文本不参与判断
    inline constexpr int kExitSuccess          = 0;   // 成功
    inline constexpr int kExitValidationFailed = 1;   // 生成前校验失败，或 --check 有不一致
    inline constexpr int kExitWriteFailed      = 2;   // 落盘失败
    inline constexpr int kExitUsage            = 3;   // 用法错误

    inline constexpr std::string_view kUsage =
        "用法: ID cmake self [--root <工程根>] [--check]\n"
        "  --root <工程根>  生成目标工程根，缺省为当前工作目录\n"
        "  --check          只比较不写盘：四份文件全部一致时退出码 0，否则 1\n"
        "退出码: 0 成功 / 1 校验失败或 --check 不一致 / 2 写入失败 / 3 用法错误\n";

    // 命令行参数：统一按 UTF-8 承载，下标 0 是程序名，与 CRT 的 argc/argv 语义一致
    using ArgumentList = std::vector<std::string>;

    // --check 读文件的结果：不存在与读取失败分开，前者按 MissingFile 报告
    enum class ReadStatus : std::uint8_t
    {
        Found    = 0,   // 读到完整内容
        NotFound = 1,   // 目标路径上没有文件
        Failed   = 2,   // 打开或读取失败
    };

    // 一次读取的结果：数据载体按值返回，不用出参回填调用方变量（cpp-style 第 7 节）
    struct FileRead
    {
        ReadStatus  status  = ReadStatus::NotFound;   // 读取结果
        std::string content = {};                     // 读到完整内容时有效
    };

    // `cmake self` 的参数解析结果：is_valid 为假表示用法错误
    struct SelfCommand
    {
        std::string root     = {};      // 工程根入参原文，空串表示未给 --root
        bool        is_check = false;   // --check：只比较不写盘
        bool        is_valid = false;   // 参数是否合法
    };

    // 工程根的解析结果：root 是原生路径归一后的 UTF-8 文本，is_valid 为假表示用法错误
    struct RootResolution
    {
        std::string root     = {};      // 工程根的 UTF-8 文本
        bool        is_valid = false;   // 目录是否存在且可访问
    };

    void print_usage()
    {
        std::cerr << kUsage;
    }

    // CLI 入参按 UTF-8 承载，转平台原生路径：与 FragmentWriter 的 project_root 契约保持同一解释方式
    std::filesystem::path to_native_path(std::string_view utf8)
    {
        std::u8string text;
        text.reserve(utf8.size());
        for (const char unit : utf8)
        {
            text.push_back(static_cast<char8_t>(static_cast<unsigned char>(unit)));
        }
        return std::filesystem::path(text);
    }

    // 平台原生路径转 UTF-8 文本：FragmentWriter 的项目根按 UTF-8 承载
    std::string to_utf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();

        std::string result;
        result.reserve(text.size());
        for (const char8_t unit : text)
        {
            result.push_back(static_cast<char>(static_cast<unsigned char>(unit)));
        }
        return result;
    }

    #if defined(_WIN32)
        // 宽字符转 UTF-8：显式按 CP_UTF8 转换，不经 ANSI 代码页；转换失败返回空串，由调用方按整条参数处理
        std::string wide_to_utf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return std::string();
            }

            const int wide_length = static_cast<int>(text.size());
            const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), wide_length, nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return std::string();
            }

            std::string result(static_cast<std::size_t>(size), '\0');
            const int   written =
                ::WideCharToMultiByte(CP_UTF8, 0, text.data(), wide_length, result.data(), size, nullptr, nullptr);
            if (written != size)
            {
                return std::string();
            }
            return result;
        }

        // 宽字符命令行取参：参数不取自 argv——Windows CRT 按 ANSI 代码页解释 argv，非 ASCII 的 --root 会指错目录。
        // CommandLineToArgvW 的返回值由 LocalAlloc 分配，必须 LocalFree；取不到时返回空清单，调用方按用法错误退出 3
        ArgumentList collect_arguments()
        {
            ArgumentList result;

            int       count = 0;
            wchar_t** wide  = ::CommandLineToArgvW(::GetCommandLineW(), &count);
            if (wide == nullptr || count <= 0)
            {
                std::cerr << "取命令行参数失败\n";
                return result;
            }

            result.reserve(static_cast<std::size_t>(count));
            for (int index = 0; index < count; ++index)
            {
                result.push_back(wide_to_utf8(wide[index]));
            }

            ::LocalFree(wide);
            return result;
        }
    #else
        // 非 Windows 平台的 argv 由运行时按 UTF-8 提供，逐项复制即可
        ArgumentList collect_arguments(int argc, char* argv[])
        {
            ArgumentList result;
            result.reserve(static_cast<std::size_t>(argc));
            for (int index = 0; index < argc; ++index)
            {
                result.push_back(argv[index]);
            }
            return result;
        }
    #endif

    void print_diagnostic(const ID::CMake::Diagnostic& diagnostic)
    {
        std::cerr << "  类别: " << ID::CMake::to_string(diagnostic.error) << '\n'
                  << "  对象: " << diagnostic.subject << '\n'
                  << "  期望: " << diagnostic.expected << '\n'
                  << "  实际: " << diagnostic.actual << '\n'
                  << "  定位: " << diagnostic.location << '\n'
                  << "  下一步: " << diagnostic.next_action << '\n';
    }

    // 每个目标文件一行：状态 + 相对 --root 的 POSIX 分隔路径
    void print_status(std::string_view status, std::string_view relative_path)
    {
        std::cout << status << ' ' << relative_path << '\n';
    }

    // 解析 `cmake self` 之后的参数：未知参数与缺取值都是用法错误（返回 is_valid 为假，调用方退出 3）
    SelfCommand parse_self_options(const ArgumentList& arguments, std::size_t first_index)
    {
        SelfCommand command;
        command.is_valid = true;

        for (std::size_t index = first_index; index < arguments.size(); ++index)
        {
            const std::string_view argument(arguments[index]);

            if (argument == "--check")
            {
                command.is_check = true;
                continue;
            }

            if (argument == "--root")
            {
                if (index + 1 >= arguments.size())
                {
                    std::cerr << "缺少 --root 的取值\n";
                    command.is_valid = false;
                    return command;
                }
                ++index;
                command.root = arguments[index];
                continue;
            }

            std::cerr << "未知参数: " << argument << '\n';
            command.is_valid = false;
            return command;
        }

        return command;
    }

    // 工程根：缺省为当前工作目录；取不到或不是目录都是用法错误
    RootResolution resolve_root(std::string_view requested)
    {
        RootResolution result;

        std::filesystem::path root;
        if (requested.empty())
        {
            std::error_code error;
            root = std::filesystem::current_path(error);
            if (error)
            {
                std::cerr << "取当前工作目录失败: " << error.message() << '\n';
                return result;
            }
        }
        else
        {
            root = to_native_path(requested);
        }

        std::error_code error;
        const bool      is_directory = std::filesystem::is_directory(root, error);
        if (error || !is_directory)
        {
            std::cerr << "--root 指向的目录不存在或不是目录: " << to_utf8(root) << '\n';
            return result;
        }

        result.root     = to_utf8(root);
        result.is_valid = true;
        return result;
    }

    // 读整个文件（二进制，逐字节比较用）：不存在与读取失败分开返回
    FileRead read_file(const std::filesystem::path& path)
    {
        FileRead result;

        std::error_code error;
        const bool      exists = std::filesystem::exists(path, error);
        if (error)
        {
            result.status = ReadStatus::Failed;
            return result;
        }
        if (!exists)
        {
            result.status = ReadStatus::NotFound;
            return result;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            result.status = ReadStatus::Failed;
            return result;
        }

        result.content.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        result.status = stream.bad() ? ReadStatus::Failed : ReadStatus::Found;
        return result;
    }

    // --check：逐文件与模型产出逐字节比较，不写盘；任一不一致即退出码 1
    int run_check(std::string_view root_text, const ID::Generate::SelfBundle& bundle)
    {
        const std::filesystem::path root           = to_native_path(root_text);
        bool                        has_difference = false;

        for (const ID::Generate::SelfFile& file : bundle.files)
        {
            const FileRead read = read_file(root / to_native_path(file.relative_path));

            if (read.status == ReadStatus::Found && read.content == file.content)
            {
                print_status("unchanged", file.relative_path);
                continue;
            }

            has_difference = true;

            if (read.status == ReadStatus::NotFound)
            {
                // 缺失不按 I/O 失败处理：它是「该文件尚未生成」，按校验失败计
                print_status("failed:MissingFile", file.relative_path);
                print_diagnostic(ID::CMake::Diagnostic{ID::CMake::CMakeError::MissingFile,
                                                       file.relative_path,
                                                       "已存在且内容与模型产出一致的生成文件",
                                                       "文件不存在",
                                                       file.relative_path,
                                                       "先运行 \"ID cmake self --root <工程根>\" 生成该文件，再重跑 --check"});
                continue;
            }

            if (read.status == ReadStatus::Failed)
            {
                print_status("failed:IoFailed", file.relative_path);
                print_diagnostic(ID::CMake::Diagnostic{ID::CMake::CMakeError::IoFailed,
                                                       file.relative_path,
                                                       "能读取该文件以比较内容",
                                                       "读取失败（可能被其它进程占用或无读权限）",
                                                       file.relative_path,
                                                       "关闭占用该文件的进程或修正权限后重跑 --check"});
                continue;
            }

            // 除「一致」外的其余情形都属「目标在生成之后被改动」，按 TargetModified 报告
            print_status("failed:TargetModified", file.relative_path);
            print_diagnostic(ID::CMake::Diagnostic{ID::CMake::CMakeError::TargetModified,
                                                   file.relative_path,
                                                   "现有内容与 emit_directory 的产出逐字节一致",
                                                   "现有内容与模型产出不同（" + std::to_string(read.content.size()) +
                                                       " 字节，模型产出 " + std::to_string(file.content.size()) + " 字节）",
                                                   file.relative_path,
                                                   "确认该文件的改动是否应保留；需要恢复生成内容时重跑 "
                                                   "\"ID cmake self --root <工程根>\""});
        }

        return has_difference ? kExitValidationFailed : kExitSuccess;
    }

    // 生成：整文件写入工具自有生成物（EmitScope::WholeFile 只允许 write_whole_file），任一失败即退出码 2
    int run_generate(std::string_view root_text, const ID::Generate::SelfBundle& bundle)
    {
        // 工程根按 UTF-8 文本交给 FragmentWriter：它的项目根契约与 CLI 入参是同一解释方式
        const std::string         project_root(root_text);
        ID::CMake::FragmentWriter writer(project_root);
        bool                      has_failure = false;

        for (const ID::Generate::SelfFile& file : bundle.files)
        {
            const ID::CMake::FragmentOutcome outcome = writer.write_whole_file(file.relative_path, file.content);

            if (ID::CMake::is_success(outcome))
            {
                // 内容未变时 writer 幂等跳过，状态行因此区分 written 与 unchanged
                print_status(outcome.is_written ? "written" : "unchanged", file.relative_path);
                continue;
            }

            has_failure = true;

            const std::string status = "failed:" + std::string(ID::CMake::to_string(outcome.diagnostic->error));
            print_status(status, file.relative_path);
            print_diagnostic(*outcome.diagnostic);
        }

        return has_failure ? kExitWriteFailed : kExitSuccess;
    }

    int run_cmake_self(const ArgumentList& arguments)
    {
        const SelfCommand command = parse_self_options(arguments, 3);
        if (!command.is_valid)
        {
            print_usage();
            return kExitUsage;
        }

        const RootResolution root = resolve_root(command.root);
        if (!root.is_valid)
        {
            print_usage();
            return kExitUsage;
        }

        const ID::Generate::SelfBundle bundle = ID::Generate::make_self_bundle();

        if (!bundle.report.is_valid)
        {
            std::cerr << "生成前校验失败，未写出任何文件\n";
            for (const ID::CMake::Diagnostic& diagnostic : bundle.report.diagnostics)
            {
                print_diagnostic(diagnostic);
            }
            return kExitValidationFailed;
        }

        return command.is_check ? run_check(root.root, bundle) : run_generate(root.root, bundle);
    }

    // `project` 分派：只组装运行上下文并调用进程内入口，命令与阶段的编排全在 `ID::Project`
    int run_project(const ArgumentList& arguments)
    {
        ID::Project::RunnerContext context;
        context.engine_root   = ID::Project::resolve_engine_root(std::string_view());
        context.registry_path = context.engine_root / "ID" / "project.json";
        context.plugin_root   = context.engine_root / "IDPlugin";
        context.io            = ID::Project::CommandIo{&std::cout, &std::cerr};

        // 环境变量只在入口读取一次：其它单元不读环境（ID_PROJECT_LOG_LEVEL 由日志器初始化自行读取）
        const char* const inject = std::getenv("ID_TEST_INJECT");
        ID::Project::init_injection(inject != nullptr ? std::string_view(inject) : std::string_view());
        context.is_injection_initialized = true;

        return ID::Project::run_command_line(std::span<const std::string>(arguments).subspan(2), context);
    }

    // 子命令分派：无参数、未知子命令与缺子命令都是用法错误（打印用法到 stderr 并退出 3）
    int run(const ArgumentList& arguments)
    {
        if (arguments.size() < 2)
        {
            print_usage();
            return kExitUsage;
        }

        const std::string_view command(arguments[1]);

        // `project` 与 `cmake self` 并列：两套用法文本分开打印，互不覆盖（spec §14.1 G5）
        if (command == "project")
        {
            return run_project(arguments);
        }

        if (command != "cmake")
        {
            std::cerr << "未知子命令: " << command << '\n';
            print_usage();
            return kExitUsage;
        }

        if (arguments.size() < 3)
        {
            std::cerr << "缺少子命令: cmake self\n";
            print_usage();
            return kExitUsage;
        }

        const std::string_view subcommand(arguments[2]);
        if (subcommand != "self")
        {
            std::cerr << "未知子命令: cmake " << subcommand << '\n';
            print_usage();
            return kExitUsage;
        }

        return run_cmake_self(arguments);
    }
} // namespace

#if defined(_WIN32)
    // 入口取 main 而不是 wmain：wmain 只有在构建期选中宽字符启动例程时才能成链（MinGW 需 -municode、
    // MSVC 需 /ENTRY:wmainCRTStartup），构建清单不属于本文件职责。宽字符命令行改由 collect_arguments
    // 内部经 GetCommandLineW + CommandLineToArgvW 取得，非 ASCII 的 --root 因此仍按 UTF-8 正确解析
    int main()
    {
        return run(collect_arguments());
    }
#else
    int main(int argc, char* argv[])
    {
        return run(collect_arguments(argc, argv));
    }
#endif
