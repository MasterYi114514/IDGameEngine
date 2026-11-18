#include "cmake/File/FragmentWriter.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

// 平台头：临时文件名要带进程唯一标识（references/memory.md 第 7 节）；Windows 分支另有目录联接的真实位置查询
#if defined(_WIN32)
    #include <process.h>

    // 只取路径查询相关声明：本分支用于解析目录联接（junction）的真实位置（见 query_final_path）
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include "cmake/Common/Assert.hpp"

namespace ID::CMake
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::size_t   kUtf8BomSize     = 3;    // 既有 BOM 只识别不求值：属标记外字节，原样保留
        constexpr std::uint32_t kMaxTempAttempts = 16;   // 同目录临时文件名已存在时的重试次数

        #if defined(_WIN32)
            // Windows 的路径长度上限：MAX_PATH 去掉结尾空字符；超长立即失败，不截断。POSIX 无此上限，不判长度
            constexpr std::size_t kMaxPathLength = 259;
        #endif

        // TU 私有的可变状态（cpp-style 第 6 节）：临时文件名序号，只在本次 .cpp 内可见，不交出地址
        std::uint64_t g_temp_sequence = 0;

        enum class ReadStatus : std::uint8_t
        {
            NotFound = 0,   // 目标路径上没有文件
            Found    = 1,   // 读到完整内容
            Failed   = 2,   // 读取失败，诊断随读文件结果一起返回（FileRead::failure）
        };

        // 受控片段在目标文件中的位置：只记录偏移与既有行尾，不改动任何字节
        struct ManagedBlockLayout
        {
            std::size_t prefix_end   = 0;       // begin 标记行（含其行尾）之后的偏移
            std::size_t suffix_start = 0;       // end 标记行的起始偏移
            std::string newline      = "\n";    // 文件第一个换行符决定的既有行尾
        };

        FragmentOutcome make_failure(CMakeError error,
                                     std::string subject,
                                     std::string expected,
                                     std::string actual,
                                     std::string location,
                                     std::string next_action)
        {
            FragmentOutcome outcome;
            outcome.diagnostic =
                Diagnostic{error, std::move(subject), std::move(expected), std::move(actual), std::move(location), std::move(next_action)};
            return outcome;
        }

        // 系统错误的文本：error_code::message() 与本地代码页绑定（zh-CN 下是 GBK），拼进 UTF-8 诊断会乱码，
        // 因此只报类别名与错误码数值——两者都是 ASCII，用户可据此查平台文档
        std::string describe_error(const std::error_code& error)
        {
            return "系统错误码 " + std::to_string(error.value()) + "（" + error.category().name() + "）";
        }

        // UTF-8 文本转平台原生路径：显式逐字节转换，不依赖 ANSI 代码页（计划书 §5.8 受控片段规则 4）
        fs::path to_path(std::string_view utf8)
        {
            std::u8string text;
            text.reserve(utf8.size());
            for (const char unit : utf8)
            {
                text.push_back(static_cast<char8_t>(static_cast<unsigned char>(unit)));
            }
            return fs::path(text);
        }

        // 平台原生路径转 UTF-8 文本：诊断与定位一律用 UTF-8，避免把本机代码页带进输出
        std::string to_utf8(const fs::path& path)
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

        // 诊断定位：相对项目根的路径在前（可迁移），解析后的绝对路径在后（便于直接定位，foundation.md 第 3 节）
        std::string located_text(const std::string& subject, const fs::path& absolute)
        {
            return subject + "（" + to_utf8(absolute) + "）";
        }

        template <typename CharType>
        CharType fold_ascii_case(CharType value) noexcept
        {
            #if defined(_WIN32)
                // Windows 文件系统路径按大小写不敏感比较：只折叠 ASCII，非 ASCII 字符按原样比较
                if (value >= static_cast<CharType>('A') && value <= static_cast<CharType>('Z'))
                {
                    return static_cast<CharType>(value - static_cast<CharType>('A') + static_cast<CharType>('a'));
                }
            #endif
            return value;
        }

        bool is_same_path_unit(const fs::path::string_type& lhs, const fs::path::string_type& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < lhs.size(); ++index)
            {
                if (fold_ascii_case(lhs[index]) != fold_ascii_case(rhs[index]))
                {
                    return false;
                }
            }
            return true;
        }

        // child 是否等于 base 或位于 base 之内：逐段比较，base 的全部段必须按顺序匹配 child 的前缀
        bool is_within(const fs::path& child, const fs::path& base)
        {
            fs::path::const_iterator child_unit = child.begin();
            for (fs::path::const_iterator base_unit = base.begin(); base_unit != base.end(); ++base_unit)
            {
                if (child_unit == child.end() || !is_same_path_unit(child_unit->native(), base_unit->native()))
                {
                    return false;
                }
                ++child_unit;
            }
            return true;
        }

        // 从绝对路径还原相对项目根的段：段比较用 is_same_path_unit，Windows 上盘符与根名大小写不同不算不匹配。
        // 不是前缀（或含空段）时返回空 optional，调用方据此报「无法从绝对路径还原相对段」。
        std::optional<fs::path> relative_to_root(const fs::path& path, const fs::path& root)
        {
            fs::path::const_iterator path_unit = path.begin();
            for (fs::path::const_iterator root_unit = root.begin(); root_unit != root.end(); ++root_unit)
            {
                if (path_unit == path.end() || !is_same_path_unit(path_unit->native(), root_unit->native()))
                {
                    return std::nullopt;
                }
                ++path_unit;
            }

            fs::path relative;
            for (; path_unit != path.end(); ++path_unit)
            {
                if (path_unit->native().empty())
                {
                    continue;   // 结尾分隔符产生的空段不计入相对路径
                }
                relative /= fs::path(path_unit->native());
            }
            return relative;
        }

        #if defined(_WIN32)
            // Windows 保留设备名：CON、PRN、AUX、NUL、COM1–COM9、LPT1–LPT9；带扩展名同样保留（CON.txt）。
            // 其它平台没有这类设备名，不判保留名（cpp-style 第 1 节：平台差异用显式条件编译分支）。
            bool is_reserved_name(std::string_view component) noexcept
            {
                std::string_view stem = component;
                const std::size_t dot = stem.find('.');
                if (dot != std::string_view::npos)
                {
                    stem = stem.substr(0, dot);
                }
                if (stem.empty() || stem.size() > 7)
                {
                    return false;
                }

                char folded[7] = {'\0', '\0', '\0', '\0', '\0', '\0', '\0'};
                for (std::size_t index = 0; index < stem.size(); ++index)
                {
                    const char unit = stem[index];
                    folded[index]   = (unit >= 'a' && unit <= 'z') ? static_cast<char>(unit - 'a' + 'A') : unit;
                }

                const std::string_view name(folded, stem.size());
                if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL" || name == "CONIN$" || name == "CONOUT$")
                {
                    return true;
                }
                if (name.size() == 4 && (name.compare(0, 3, "COM") == 0 || name.compare(0, 3, "LPT") == 0))
                {
                    const char digit = name[3];
                    return digit >= '1' && digit <= '9';
                }
                return false;
            }
        #endif

        #if defined(_WIN32)
            // 真实路径：MinGW GCC 的 std::filesystem 不解析目录联接（junction）——canonical 原样返回链接路径、
            // is_symlink 恒为 false，因此「不跟随符号链接写到项目外」只能按真实路径判定（references/memory.md 第 6 节）。
            // 这里用 GetFinalPathNameByHandleW 取已存在路径的真实位置；取不到时回退 std::filesystem。
            std::optional<fs::path> query_final_path(const fs::path& path)
            {
                HANDLE handle = ::CreateFileW(path.c_str(),
                                              0,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                              nullptr,
                                              OPEN_EXISTING,
                                              FILE_FLAG_BACKUP_SEMANTICS,
                                              nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return std::nullopt;
                }

                std::wstring buffer(static_cast<std::size_t>(MAX_PATH), L'\0');
                DWORD        length = 0;
                for (;;)
                {
                    length = ::GetFinalPathNameByHandleW(handle,
                                                         buffer.data(),
                                                         static_cast<DWORD>(buffer.size()),
                                                         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                    if (length == 0)
                    {
                        ::CloseHandle(handle);
                        return std::nullopt;
                    }
                    if (length < buffer.size())
                    {
                        break;
                    }
                    buffer.resize(static_cast<std::size_t>(length) + 1);
                }
                ::CloseHandle(handle);
                buffer.resize(length);

                // 去掉 \\?\ 与 \\?\UNC\ 前缀，得到可直接与项目根做前缀比较的普通路径
                constexpr std::wstring_view kDevicePrefix = L"\\\\?\\";
                constexpr std::wstring_view kUncPrefix    = L"\\\\?\\UNC\\";
                if (buffer.compare(0, kUncPrefix.size(), kUncPrefix) == 0)
                {
                    buffer = L"\\\\" + buffer.substr(kUncPrefix.size());
                }
                else if (buffer.compare(0, kDevicePrefix.size(), kDevicePrefix) == 0)
                {
                    buffer = buffer.substr(kDevicePrefix.size());
                }

                return fs::path(buffer);
            }
        #endif

        // 取路径的真实位置：解引用符号链接与目录联接；不存在的尾部接回最近存在的祖先之后。
        // 取不到时返回空 optional（不用出参回填调用方变量）。
        std::optional<fs::path> resolve_real_path(const fs::path& path)
        {
            std::error_code ec;
            fs::path        existing = path;
            fs::path        tail;
            while (!existing.empty() && !fs::exists(existing, ec))
            {
                const fs::path parent = existing.parent_path();
                if (parent == existing)
                {
                    break;
                }
                const fs::path name = existing.filename();
                tail                = tail.empty() ? name : (name / tail);
                existing            = parent;
            }
            if (existing.empty())
            {
                return std::nullopt;
            }

            std::optional<fs::path> real_existing;
            #if defined(_WIN32)
                real_existing = query_final_path(existing);
            #endif
            if (!real_existing.has_value())
            {
                real_existing = fs::weakly_canonical(existing, ec);
                if (ec)
                {
                    return std::nullopt;
                }
            }

            return tail.empty() ? *real_existing : (*real_existing / tail);
        }

        // 读目标文件的结果：状态、内容与失败诊断一起按值返回（cpp-style 第 7 节：不用出参回填调用方变量）
        struct FileRead
        {
            ReadStatus      status  = ReadStatus::NotFound;   // 读取结论
            std::string     data    = {};                     // status 为 Found 时的完整内容
            FragmentOutcome failure = {};                     // status 为 Failed 时的诊断
        };

        FileRead read_file_if_exists(const fs::path& path, const std::string& subject)
        {
            FileRead        result;
            std::error_code ec;
            const fs::file_status status = fs::status(path, ec);
            if (ec == std::errc::no_such_file_or_directory)
            {
                return result;
            }
            if (ec)
            {
                result.status  = ReadStatus::Failed;
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "能查询目标路径的状态",
                                              "查询状态失败：" + describe_error(ec),
                                              located_text(subject, path),
                                              "修正目标路径中不存在的中间目录或权限问题后重跑本次生成");
                return result;
            }
            if (status.type() == fs::file_type::not_found)
            {
                return result;
            }
            if (fs::is_directory(status))
            {
                result.status  = ReadStatus::Failed;
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "目标是普通文件",
                                              "目标路径是目录",
                                              located_text(subject, path),
                                              "把写入路径改成该目录下的文件名，例如 \"<目录>/CMakeLists.txt\" 后重跑");
                return result;
            }

            std::ifstream stream(path, std::ios::binary);
            if (!stream.is_open())
            {
                result.status  = ReadStatus::Failed;
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "能打开目标文件读取",
                                              "打开失败（可能被其它进程独占或无读权限）",
                                              located_text(subject, path),
                                              "关闭占用该文件的进程或修正权限设置后重跑本次生成");
                return result;
            }

            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0)
            {
                result.status  = ReadStatus::Failed;
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "能取得目标文件长度",
                                              "取长度失败",
                                              located_text(subject, path),
                                              "确认目标文件是可读普通文件（不是设备或管道）后重跑");
                return result;
            }

            result.data.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (size > 0)
            {
                stream.read(result.data.data(), static_cast<std::streamsize>(size));
                if (stream.gcount() != static_cast<std::streamsize>(size))
                {
                    result.status  = ReadStatus::Failed;
                    result.failure = make_failure(CMakeError::IoFailed,
                                                  subject,
                                                  "读满 " + std::to_string(static_cast<std::size_t>(size)) + " 字节",
                                                  "实际读到 " + std::to_string(static_cast<std::size_t>(stream.gcount())) + " 字节",
                                                  located_text(subject, path),
                                                  "确认该文件在读取期间未被其它进程截断后重跑本次生成");
                    return result;
                }
            }
            result.status = ReadStatus::Found;
            return result;
        }

        // 目标路径解析的结果：结论、实际写入路径与失败诊断（cpp-style 第 7 节，不用出参）
        struct TargetResolution
        {
            bool            is_resolved = false;   // 是否为项目根内可写入的目标
            fs::path        absolute    = {};      // is_resolved 为 true 时实际写入的路径（已解引用符号链接）
            FragmentOutcome failure     = {};      // is_resolved 为 false 时的诊断
        };

        TargetResolution resolve_target(const fs::path& root, const std::string& relative_path)
        {
            TargetResolution result;
            // 诊断的六字段必须齐全：空路径没有可打印的对象名，用显式占位符，不留下空字段
            const std::string subject = relative_path.empty() ? std::string("<空路径>") : relative_path;
            if (relative_path.empty())
            {
                result.failure = make_failure(CMakeError::PathEscapesProjectRoot,
                                              subject,
                                              "相对项目根的非空文件路径",
                                              "空路径",
                                              subject,
                                              "传入相对项目根的路径，例如 \"CMakeLists.txt\" 后重跑");
                return result;
            }

            std::error_code ec;
            const fs::path root_absolute = fs::absolute(root, ec).lexically_normal();
            if (ec)
            {
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "项目根能解析为绝对路径",
                                              "解析项目根失败：" + describe_error(ec),
                                              subject,
                                              "确认 FragmentWriter 构造时传入的项目根目录存在且可访问后重跑");
                return result;
            }

            const std::optional<fs::path> root_real = resolve_real_path(root_absolute);
            if (!root_real.has_value())
            {
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "项目根目录存在",
                                              "项目根不存在或无法解析真实路径",
                                              subject,
                                              "创建该目录后重跑，或用存在的目录作为 FragmentWriter 的项目根");
                return result;
            }

            // 词法包含：解析后的路径必须在项目根之内，``..`` 逃逸与项目外绝对路径在此拒绝
            const fs::path raw       = to_path(relative_path);
            const fs::path candidate = (raw.is_absolute() ? raw : root_absolute / raw).lexically_normal();
            if (!is_within(candidate, root_absolute))
            {
                result.failure = make_failure(CMakeError::PathEscapesProjectRoot,
                                              subject,
                                              "解析后的路径仍位于项目根 " + to_utf8(root_absolute) + " 之内",
                                              "解析结果是 " + to_utf8(candidate),
                                              subject,
                                              "去掉路径中的 .. 段或改用项目根内的相对路径（如 \"CMakeLists.txt\"）后重跑");
                return result;
            }

            // 平台限制（路径长度、保留名）在解析真实路径之前判定：既避免打开设备名，也让超长路径不进文件系统调用。
            // 相对段按段比较还原：Windows 上绝对路径的盘符或根名大小写不同不算不匹配，也不误报为空段。
            const std::optional<fs::path> lexical_part = relative_to_root(candidate, root_absolute);
            if (!lexical_part.has_value() || lexical_part->empty() || *lexical_part == fs::path("."))
            {
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "指向项目根内的某个文件",
                                              "无法从绝对路径还原相对段",
                                              subject,
                                              "传入目标文件的相对路径，例如 \"CMakeLists.txt\" 后重跑");
                return result;
            }

            #if defined(_WIN32)
                // 超长路径立即失败并给建议，不静默截断（references/memory.md 第 6 节）
                const std::size_t path_length = candidate.native().size();
                if (path_length > kMaxPathLength)
                {
                    result.failure = make_failure(CMakeError::IoFailed,
                                                  subject,
                                                  "完整路径不超过 " + std::to_string(kMaxPathLength) + " 个字符",
                                                  "完整路径有 " + std::to_string(path_length) + " 个字符：" + to_utf8(candidate),
                                                  subject,
                                                  "缩短项目根所在层级或缩短相对路径后重跑；Windows 默认路径上限为 MAX_PATH=260");
                    return result;
                }

                // 平台保留名立即失败：这类路径在 Windows 上会打开设备而不是文件
                for (const auto& unit : *lexical_part)
                {
                    const std::string name = to_utf8(unit);
                    if (is_reserved_name(name))
                    {
                        result.failure = make_failure(CMakeError::IoFailed,
                                                      subject,
                                                      "路径各段不是 Windows 保留设备名",
                                                      "路径段 \"" + name + "\" 是保留设备名",
                                                      subject,
                                                      "把该路径段改成非保留名（例如 \"" + name + "_out\"）后重跑");
                        return result;
                    }
                }
            #endif

            // 真实路径包含：符号链接（含目录联接）指向项目外时拒绝，写不进项目根之外
            const std::optional<fs::path> target_real = resolve_real_path(candidate);
            if (!target_real.has_value())
            {
                result.failure = make_failure(CMakeError::IoFailed,
                                              subject,
                                              "目标路径可解析真实路径",
                                              "解析真实路径失败",
                                              subject,
                                              "确认路径各级目录未被删除、符号链接目标存在后重跑");
                return result;
            }
            if (!is_within(*target_real, *root_real))
            {
                result.failure = make_failure(CMakeError::PathEscapesProjectRoot,
                                              subject,
                                              "解引用符号链接后仍在项目根 " + to_utf8(*root_real) + " 之内",
                                              "解引用后落在 " + to_utf8(*target_real),
                                              subject,
                                              "把该符号链接改为指向项目根内的目录，或直接用项目根内的实体路径后重跑");
                return result;
            }

            result.is_resolved = true;
            result.absolute    = *target_real;
            return result;
        }

        // 父目录按需创建：成功返回空结果（诊断为空即通过），失败返回 IoFailed 诊断
        FragmentOutcome create_directory_if_needed(const fs::path& parent, const std::string& subject)
        {
            std::error_code ec;
            if (fs::is_directory(parent, ec))
            {
                return FragmentOutcome{};
            }

            ec.clear();
            fs::create_directories(parent, ec);
            if (ec || !fs::is_directory(parent, ec))
            {
                return make_failure(CMakeError::IoFailed,
                                    subject,
                                    "父目录可按需创建：" + to_utf8(parent),
                                    ec ? ("创建父目录失败：" + describe_error(ec)) : "父目录创建后仍不是目录",
                                    located_text(subject, parent),
                                    "手工创建该父目录或修正路径中的非法字符后重跑本次生成");
            }
            return FragmentOutcome{};
        }

        // 进程唯一标识：临时文件名要带它（references/memory.md 第 7 节）；两个平台各用自己的系统调用
        long current_process_id() noexcept
        {
            #if defined(_WIN32)
                return static_cast<long>(::_getpid());
            #else
                return static_cast<long>(::getpid());
            #endif
        }

        // 临时文件的写出结果：是否创建、临时文件路径与失败诊断
        struct TempFile
        {
            bool            is_created = false;   // 临时文件是否已写出
            fs::path        path       = {};      // is_created 为 true 时的临时文件路径
            FragmentOutcome failure    = {};      // is_created 为 false 时的诊断
        };

        // 同目录临时文件：文件名带进程唯一标识与进程内序号，写完后才替换目标
        TempFile write_temp_file(const fs::path& parent,
                                 const fs::path& target_name,
                                 std::string_view data,
                                 const std::string& subject)
        {
            const long        process_id = current_process_id();
            const std::string base_name  = to_utf8(target_name);

            TempFile result;
            for (std::uint32_t attempt = 0; attempt < kMaxTempAttempts; ++attempt)
            {
                ++g_temp_sequence;
                const std::string temp_name =
                    base_name + ".idtmp." + std::to_string(process_id) + "." + std::to_string(g_temp_sequence);
                const fs::path candidate = parent / to_path(temp_name);

                std::error_code ec;
                if (fs::exists(candidate, ec))
                {
                    continue;   // 不覆盖任何不是本次创建的文件
                }

                std::ofstream stream(candidate, std::ios::binary | std::ios::trunc);
                if (!stream.is_open())
                {
                    result.failure = make_failure(CMakeError::IoFailed,
                                                  subject,
                                                  "能在目标目录创建临时文件：" + to_utf8(candidate),
                                                  "创建临时文件失败（目录只读或不可写）",
                                                  located_text(subject, parent),
                                                  "给该目录写权限后重跑本次生成");
                    return result;
                }

                if (!data.empty())
                {
                    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
                }
                stream.flush();
                const bool is_write_ok = static_cast<bool>(stream);
                stream.close();
                if (!is_write_ok || stream.fail())
                {
                    std::error_code ignored;
                    fs::remove(candidate, ignored);   // 只删本次创建的半成品
                    result.failure = make_failure(CMakeError::IoFailed,
                                                  subject,
                                                  "临时文件写入并关闭成功",
                                                  "写入或关闭临时文件失败",
                                                  located_text(subject, parent),
                                                  "确认目标目录可写且磁盘空间足够后重跑本次生成");
                    return result;
                }

                result.is_created = true;
                result.path       = candidate;
                return result;
            }

            result.failure = make_failure(CMakeError::IoFailed,
                                          subject,
                                          "同目录临时文件名唯一",
                                          "连续 " + std::to_string(kMaxTempAttempts) + " 次尝试都被已存在的同名文件占用",
                                          located_text(subject, parent),
                                          "清理目标目录下残留的 \"*.idtmp.*\" 文件（早前异常退出的本进程产物）后重跑");
            return result;
        }

        // 替换前重读目标：与本次读取的快照不一致即 TargetModified，不替换。
        // 成功返回空结果（诊断为空即通过），失败返回诊断。
        FragmentOutcome verify_unchanged(const fs::path& target,
                                         std::string_view snapshot,
                                         bool snapshot_existed,
                                         const std::string& subject)
        {
            FileRead read = read_file_if_exists(target, subject);
            if (read.status == ReadStatus::Failed)
            {
                return std::move(read.failure);
            }
            if (!snapshot_existed && read.status == ReadStatus::NotFound)
            {
                return FragmentOutcome{};
            }
            if (snapshot_existed && read.status == ReadStatus::Found && static_cast<std::string_view>(read.data) == snapshot)
            {
                return FragmentOutcome{};
            }

            return make_failure(CMakeError::TargetModified,
                                subject,
                                "目标内容与本次读取时的快照一致",
                                (read.status == ReadStatus::NotFound) ? "目标在替换前被删除" : "目标在替换前被其它进程改写",
                                located_text(subject, target),
                                "确认没有其它进程或工具正在写该文件，然后重跑本次生成");
        }

        FragmentOutcome commit_content(const fs::path& target_absolute,
                                       std::string_view snapshot,
                                       bool snapshot_existed,
                                       std::string new_content,
                                       const std::string& subject)
        {
            const fs::path parent = target_absolute.parent_path();
            if (parent.empty())
            {
                return make_failure(CMakeError::IoFailed,
                                    subject,
                                    "目标路径含父目录",
                                    "解析结果是驱动器根或没有父目录",
                                    located_text(subject, target_absolute),
                                    "改用项目根内带父目录的相对路径后重跑");
            }

            FragmentOutcome created = create_directory_if_needed(parent, subject);
            if (!is_success(created))
            {
                return created;
            }

            TempFile temp = write_temp_file(parent, target_absolute.filename(), new_content, subject);
            if (!temp.is_created)
            {
                return std::move(temp.failure);
            }

            FragmentOutcome verified = verify_unchanged(target_absolute, snapshot, snapshot_existed, subject);
            if (!is_success(verified))
            {
                std::error_code ignored;
                fs::remove(temp.path, ignored);
                return verified;
            }

            std::error_code ec;
            fs::rename(temp.path, target_absolute, ec);
            if (ec)
            {
                std::error_code ignored;
                fs::remove(temp.path, ignored);
                return make_failure(CMakeError::IoFailed,
                                    subject,
                                    "临时文件替换目标文件成功",
                                    "替换失败：" + describe_error(ec),
                                    located_text(subject, target_absolute),
                                    "确认目标文件未被其它进程占用后重跑本次生成");
            }

            FragmentOutcome outcome;
            outcome.is_written = true;
            outcome.bytes      = new_content.size();
            return outcome;
        }

        std::size_t skip_utf8_bom(std::string_view text) noexcept
        {
            if (text.size() >= kUtf8BomSize && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
            {
                return kUtf8BomSize;
            }
            return 0;
        }

        // 既有行尾由文件第一个换行符决定；单行文件没有既有行尾，按 emitter 的 '\n' 写
        std::string detect_newline(std::string_view text, std::size_t body_start)
        {
            const std::size_t newline = text.find('\n', body_start);
            if (newline == std::string_view::npos)
            {
                return "\n";
            }
            if (newline > body_start && text[newline - 1] == '\r')
            {
                return "\r\n";
            }
            return "\n";
        }

        // 受控片段定位的结果：结论、偏移与既有行尾、失败诊断（cpp-style 第 7 节，不用出参）
        struct BlockAnalysis
        {
            bool               is_analyzed = false;   // begin/end 标记恰好一组且顺序正确
            ManagedBlockLayout layout      = {};      // is_analyzed 为 true 时的偏移与既有行尾
            FragmentOutcome    failure     = {};      // 标记缺失、重复或顺序错误时的诊断
        };

        BlockAnalysis analyze_managed_block(std::string_view text,
                                            std::string_view begin_marker,
                                            std::string_view end_marker,
                                            const std::string& subject)
        {
            BlockAnalysis     result;
            const std::size_t body_start = skip_utf8_bom(text);

            std::size_t begin_count = 0;
            std::size_t begin_start = 0;
            std::size_t begin_next  = 0;
            std::size_t begin_line  = 0;
            std::size_t end_count   = 0;
            std::size_t end_start   = 0;
            std::size_t end_line    = 0;

            std::size_t index       = body_start;
            std::size_t line_number = 1;
            while (index < text.size())
            {
                const std::size_t newline    = text.find('\n', index);
                const std::size_t line_end   = (newline == std::string_view::npos) ? text.size() : newline;
                const std::size_t next_start = (newline == std::string_view::npos) ? text.size() : newline + 1;

                std::string_view line = text.substr(index, line_end - index);
                if (!line.empty() && line.back() == '\r')
                {
                    line.remove_suffix(1);
                }

                // 两个标记各自独立计数：同一行同时等于两种标记时留给顺序判定报 AmbiguousMarker
                if (line == begin_marker)
                {
                    ++begin_count;
                    begin_start = index;
                    begin_next  = next_start;
                    begin_line  = line_number;
                }
                if (line == end_marker)
                {
                    ++end_count;
                    end_start = index;
                    end_line  = line_number;
                }

                index = next_start;
                ++line_number;
            }

            if (begin_count == 0 || end_count == 0)
            {
                std::string missing = "begin 与 end 标记都缺失";
                if (begin_count == 0 && end_count > 0)
                {
                    missing = "缺 begin 标记";
                }
                else if (begin_count > 0 && end_count == 0)
                {
                    missing = "缺 end 标记";
                }

                // 定位序号只在至少命中一条标记时才有意义；两条都缺失时按文件级定位
                const std::string location = (begin_count > 0) ? (subject + ":" + std::to_string(begin_line))
                                           : (end_count > 0)   ? (subject + ":" + std::to_string(end_line))
                                                               : subject;
                result.failure             = make_failure(CMakeError::MissingMarker,
                                                          subject,
                                                          std::string(begin_marker) + " 与 " + std::string(end_marker) +
                                                              " 各恰好一行、行首精确匹配且该行无其它字符",
                                                          missing + "（begin 命中 " + std::to_string(begin_count) + " 次，end 命中 " +
                                                              std::to_string(end_count) + " 次）",
                                                          location,
                                                          "在 " + subject + " 中按行补上缺失的标记（" + std::string(begin_marker) + "、或 " +
                                                              std::string(end_marker) + " 各独占一行，行首不留其它字符）后重跑本次生成");
                return result;
            }

            if (begin_count > 1 || end_count > 1)
            {
                const std::size_t line_hint = (begin_count > 1) ? begin_line : end_line;
                result.failure              = make_failure(CMakeError::AmbiguousMarker,
                                                           subject,
                                                           "begin 与 end 标记各恰好一条",
                                                           "begin 命中 " + std::to_string(begin_count) + " 次，end 命中 " +
                                                               std::to_string(end_count) + " 次",
                                                           subject + ":" + std::to_string(line_hint),
                                                           "删除多余的同名标记行，只保留一组 " + std::string(begin_marker) + " / " +
                                                               std::string(end_marker) + " 后重跑本次生成");
                return result;
            }

            if (!(begin_start < end_start))
            {
                result.failure = make_failure(CMakeError::AmbiguousMarker,
                                              subject,
                                              "begin 标记在 end 标记之前",
                                              "begin 在第 " + std::to_string(begin_line) + " 行，end 在第 " +
                                                  std::to_string(end_line) + " 行",
                                              subject + ":" + std::to_string(end_line),
                                              "把 " + std::string(end_marker) + " 行移到 " + std::string(begin_marker) +
                                                  " 行之后（中间放本工具维护的片段）后重跑");
                return result;
            }

            result.is_analyzed         = true;
            result.layout.prefix_end   = begin_next;
            result.layout.suffix_start = end_start;
            result.layout.newline      = detect_newline(text, body_start);
            return result;
        }

        // 片段内容按文件既有行尾展开；标记之外的内容按原字符串逐字复制
        std::string compose_managed_block(std::string_view existing, const ManagedBlockLayout& layout, std::string_view content)
        {
            std::string result;
            result.reserve(existing.size() + content.size() + 16);
            result.append(existing.substr(0, layout.prefix_end));

            bool is_content_empty  = content.empty();
            bool ends_with_newline = is_content_empty;
            std::size_t index      = 0;
            while (index < content.size())
            {
                const char unit = content[index];
                if (unit == '\r' && index + 1 < content.size() && content[index + 1] == '\n')
                {
                    ++index;   // 调用方传入的 CRLF 按一次换行处理，避免写出 CRCRLF
                    continue;
                }
                if (unit == '\n')
                {
                    result.append(layout.newline);
                    ends_with_newline = true;
                }
                else
                {
                    result.push_back(unit);
                    ends_with_newline = false;
                }
                ++index;
            }

            // end 标记必须从行首开始：片段非空且没有以换行结尾时补一个既有行尾
            if (!is_content_empty && !ends_with_newline)
            {
                result.append(layout.newline);
            }

            result.append(existing.substr(layout.suffix_start));
            return result;
        }
    } // namespace

    bool is_success(const FragmentOutcome& outcome) noexcept
    {
        return !outcome.diagnostic.has_value();
    }

    FragmentWriter::FragmentWriter(std::string project_root)
        : m_project_root(std::move(project_root))
    {
    }

    FragmentOutcome FragmentWriter::write_whole_file(const std::string& relative_path, std::string_view content)
    {
        const fs::path   root   = to_path(m_project_root);
        TargetResolution target = resolve_target(root, relative_path);
        if (!target.is_resolved)
        {
            return std::move(target.failure);
        }

        FileRead read = read_file_if_exists(target.absolute, relative_path);
        if (read.status == ReadStatus::Failed)
        {
            return std::move(read.failure);
        }

        const bool had_existing = (read.status == ReadStatus::Found);
        if (had_existing && static_cast<std::string_view>(read.data) == content)
        {
            return FragmentOutcome{};   // 幂等：内容相同不重写，不产生诊断
        }

        return commit_content(target.absolute, read.data, had_existing, std::string(content), relative_path);
    }

    FragmentOutcome FragmentWriter::write_managed_block(const std::string& relative_path,
                                                        std::string_view content,
                                                        std::string_view begin_marker,
                                                        std::string_view end_marker)
    {
        // 标记由组装阶段提供：空标记或含换行的标记无法按行匹配，属调用方违约（不进退诊断通道）
        ID_ASSERT(!begin_marker.empty() && !end_marker.empty());
        ID_ASSERT(begin_marker.find('\r') == std::string_view::npos && begin_marker.find('\n') == std::string_view::npos);
        ID_ASSERT(end_marker.find('\r') == std::string_view::npos && end_marker.find('\n') == std::string_view::npos);

        const fs::path   root   = to_path(m_project_root);
        TargetResolution target = resolve_target(root, relative_path);
        if (!target.is_resolved)
        {
            return std::move(target.failure);
        }

        FileRead read = read_file_if_exists(target.absolute, relative_path);
        if (read.status == ReadStatus::Failed)
        {
            return std::move(read.failure);
        }
        if (read.status == ReadStatus::NotFound)
        {
            return make_failure(CMakeError::MissingFile,
                                relative_path,
                                "已存在且含 begin/end 标记的目标文件（受控片段写入不创建文件）",
                                "目标文件不存在",
                                located_text(relative_path, target.absolute),
                                "先创建 " + relative_path + " 并写入 " + std::string(begin_marker) + " 与 " +
                                    std::string(end_marker) + " 两行标记后重跑本次生成");
        }

        BlockAnalysis analysis = analyze_managed_block(read.data, begin_marker, end_marker, relative_path);
        if (!analysis.is_analyzed)
        {
            return std::move(analysis.failure);
        }

        std::string updated = compose_managed_block(read.data, analysis.layout, content);
        if (updated == read.data)
        {
            return FragmentOutcome{};   // 幂等：片段内容未变化，标记外字节本来就一致
        }

        return commit_content(target.absolute, read.data, true, std::move(updated), relative_path);
    }

    // ---------- 多片段写入与只读快照 ----------

    namespace
    {
        // 解析结论 → 写入期诊断类别：一个结论只映射一个类别，不新增类别（标记缺失与不成对都归 MissingMarker）
        CMakeError layout_error(FragmentState state) noexcept
        {
            switch (state)
            {
                case FragmentState::Ambiguous:  return CMakeError::AmbiguousMarker;
                case FragmentState::OutOfOrder: return CMakeError::DuplicateFragment;
                case FragmentState::Missing:    return CMakeError::MissingMarker;
                case FragmentState::Incomplete: return CMakeError::MissingMarker;
                case FragmentState::Ok:         break;
            }
            return CMakeError::MissingMarker;
        }

        Diagnostic make_layout_diagnostic(FragmentState state,
                                          const std::string& detail,
                                          const std::string& subject,
                                          const fs::path& target)
        {
            return Diagnostic{layout_error(state), subject,
                              "每个片段的 BEGIN 与 END 标记各一条、BEGIN 在 END 之前，且片段范围不重叠", detail,
                              located_text(subject, target),
                              "按受控片段标记规则修正该文件（补齐或删除标记行、调整片段顺序）后重跑本次生成"};
        }

        // 片段体：范围字节里去掉首行（BEGIN 标记行）与末行（END 标记行）之后的全部字节。
        // 只做切片，不重做标记解析——标记位置由 parse_fragments 给出
        std::string_view fragment_body(const FragmentRange& range) noexcept
        {
            const std::size_t first_newline = range.bytes.find('\n');
            if (first_newline == std::string::npos)
            {
                return std::string_view();
            }
            const std::size_t body_begin = first_newline + 1;

            std::size_t last_line_end = range.bytes.size();
            if (last_line_end > 0 && range.bytes[last_line_end - 1] == '\n')
            {
                --last_line_end;
            }

            std::size_t last_line_start = body_begin;
            for (std::size_t index = last_line_end; index > body_begin; --index)
            {
                if (range.bytes[index - 1] == '\n')
                {
                    last_line_start = index;
                    break;
                }
            }
            if (last_line_start <= body_begin)
            {
                return std::string_view();
            }
            return std::string_view(range.bytes).substr(body_begin, last_line_start - body_begin);
        }

        // 全成或全不成：任一片段失败时所有元素零写、摘要为空、带同一条失败原因
        void fill_failure(std::vector<ManagedWriteResult>& results, const Diagnostic& diagnostic)
        {
            for (ManagedWriteResult& result : results)
            {
                result.is_written   = false;
                result.range_digest.clear();
                result.diagnostic   = diagnostic;
            }
        }

        // 既有单入口的失败结果 → 多片段结果的全部失败：失败结果按契约必然带诊断，
        // 万一为空按 IoFailed 兜底，不对空 optional 解引用
        void fill_outcome_failure(std::vector<ManagedWriteResult>& results, const FragmentOutcome& failure)
        {
            if (failure.diagnostic.has_value())
            {
                fill_failure(results, *failure.diagnostic);
                return;
            }
            fill_failure(results, Diagnostic{CMakeError::IoFailed, "<未知目标>", "失败时附带诊断", "缺少诊断信息", "",
                                             "重跑本次生成；若仍无诊断，检查写入失败路径是否漏填诊断"});
        }
    } // namespace

    std::vector<ManagedWriteResult> FragmentWriter::write_managed_blocks(const std::string& relative_path,
                                                                         std::span<const ManagedBlock> blocks)
    {
        std::vector<ManagedWriteResult> results;
        results.reserve(blocks.size());
        for (const ManagedBlock& block : blocks)
        {
            ManagedWriteResult result;
            result.fragment = block.name;
            results.push_back(std::move(result));
        }

        if (blocks.empty())
        {
            return results;
        }

        const fs::path    root    = to_path(m_project_root);
        const std::string subject = relative_path.empty() ? std::string("<空路径>") : relative_path;

        TargetResolution target = resolve_target(root, relative_path);
        if (!target.is_resolved)
        {
            fill_outcome_failure(results, target.failure);
            return results;
        }

        // 重复片段名：一次写入里同一片段只能出现一次，否则"按文件位置组合"没有唯一解释
        for (std::size_t index = 0; index < blocks.size(); ++index)
        {
            for (std::size_t earlier = 0; earlier < index; ++earlier)
            {
                if (blocks[earlier].name != blocks[index].name)
                {
                    continue;
                }

                fill_failure(results, Diagnostic{CMakeError::DuplicateFragment, subject,
                                                 "每个片段名在一次多片段写入里只出现一次",
                                                 "片段 " + blocks[index].name + " 重复给出",
                                                 located_text(subject, target.absolute),
                                                 "从 blocks 里删掉重复的片段后重跑本次生成"});
                return results;
            }
        }

        FileRead read = read_file_if_exists(target.absolute, relative_path);
        if (read.status == ReadStatus::Failed)
        {
            fill_outcome_failure(results, read.failure);
            return results;
        }
        if (read.status == ReadStatus::NotFound)
        {
            fill_failure(results, Diagnostic{CMakeError::MissingFile, subject,
                                             "已存在且含各片段标记的目标文件（多片段写入不创建文件）",
                                             "目标文件不存在", located_text(subject, target.absolute),
                                             "先按首次创建写出该文件（含全部片段标记）后重跑本次生成"});
            return results;
        }

        // 片段名与内容按 blocks 顺序登记；名称视图指向 blocks 元素，调用期间有效
        std::vector<std::string_view> names;
        std::vector<std::pair<std::string_view, std::string_view>> replacements;
        names.reserve(blocks.size());
        replacements.reserve(blocks.size());
        for (const ManagedBlock& block : blocks)
        {
            names.emplace_back(block.name);
            replacements.emplace_back(std::string_view(block.name), std::string_view(block.content));
        }

        const FragmentLayout layout = parse_fragments(read.data, names);
        if (layout.state != FragmentState::Ok)
        {
            fill_failure(results, make_layout_diagnostic(layout.state, layout.detail, subject, target.absolute));
            return results;
        }

        const std::optional<std::string> composed = compose_fragments(read.data, layout, replacements);
        if (!composed.has_value())
        {
            fill_failure(results, Diagnostic{CMakeError::DuplicateFragment, subject,
                                             "每个片段都能在布局里找到位置、且范围落在本次读取的字节内",
                                             "片段范围与本次读取的字节不同源", located_text(subject, target.absolute),
                                             "重新读取该文件后重跑本次生成"});
            return results;
        }

        // 写完后的文件必须仍能被同一套标记规则解析：新内容里若出现同名标记行，这里拦住（零写）
        const FragmentLayout new_layout = parse_fragments(*composed, names);
        if (new_layout.state != FragmentState::Ok)
        {
            fill_failure(results, make_layout_diagnostic(new_layout.state, new_layout.detail, subject, target.absolute));
            return results;
        }

        if (*composed == read.data)
        {
            // 全部片段都与现状一致：不替换、不产生诊断；逐片段 is_written 保持 false
            return results;
        }

        // 逐片段判定"本次是否真的写入新字节"：把该片段换成新内容、其余片段保持现状后重新组合，
        // 组合结果与现状相同即说明该片段内容没有变化（组合只有 compose_fragments 一处实现）
        for (std::size_t index = 0; index < blocks.size(); ++index)
        {
            std::vector<std::pair<std::string_view, std::string_view>> single;
            single.reserve(layout.ranges.size());
            for (const FragmentRange& range : layout.ranges)
            {
                if (range.name == blocks[index].name)
                {
                    single.emplace_back(std::string_view(range.name), std::string_view(blocks[index].content));
                    continue;
                }
                single.emplace_back(std::string_view(range.name), fragment_body(range));
            }

            const std::optional<std::string> unchanged = compose_fragments(read.data, layout, single);
            results[index].is_written = !unchanged.has_value() || (*unchanged != read.data);
        }

        const FragmentOutcome written = commit_content(target.absolute, read.data, true, *composed, relative_path);
        if (!is_success(written))
        {
            fill_outcome_failure(results, written);
            return results;
        }

        // 写出后的片段摘要只经 fragment_range_digest 计算，对象是写后字节里的片段范围
        for (ManagedWriteResult& result : results)
        {
            if (!result.is_written)
            {
                continue;
            }
            for (const FragmentRange& range : new_layout.ranges)
            {
                if (range.name == result.fragment)
                {
                    result.range_digest = fragment_range_digest(*composed, range);
                    break;
                }
            }
        }

        return results;
    }

    FragmentSnapshot FragmentWriter::read_fragments(const std::string& relative_path,
                                                    std::span<const std::string_view> names)
    {
        FragmentSnapshot snapshot;

        const fs::path         root    = to_path(m_project_root);
        const std::string      subject = relative_path.empty() ? std::string("<空路径>") : relative_path;
        const TargetResolution target  = resolve_target(root, relative_path);
        if (!target.is_resolved)
        {
            snapshot.detail     = "目标路径无法解析为项目根内的文件";
            snapshot.diagnostic = target.failure.diagnostic;
            return snapshot;
        }

        FileRead read = read_file_if_exists(target.absolute, relative_path);
        if (read.status == ReadStatus::Failed)
        {
            snapshot.detail     = "目标文件读取失败";
            snapshot.diagnostic = read.failure.diagnostic;
            return snapshot;
        }
        if (read.status == ReadStatus::NotFound)
        {
            snapshot.state  = FragmentState::Missing;
            snapshot.detail = "目标文件不存在";
            return snapshot;
        }

        snapshot.file_bytes = std::move(read.data);

        // 整文件摘要复用片段摘要的唯一入口：把整个文件当成一个范围
        FragmentRange whole;
        whole.offset = 0;
        whole.length = snapshot.file_bytes.size();
        snapshot.file_digest = fragment_range_digest(snapshot.file_bytes, whole);

        const FragmentLayout layout = parse_fragments(snapshot.file_bytes, names);
        snapshot.state  = layout.state;
        snapshot.detail = layout.detail;
        snapshot.ranges = layout.ranges;
        return snapshot;
    }
} // namespace ID::CMake
