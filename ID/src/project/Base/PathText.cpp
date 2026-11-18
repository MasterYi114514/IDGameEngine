#include "project/Base/PathText.hpp"

#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

#include "Core/Core.hpp"

#include "project/Base/Text.hpp"

// 平台差异只在本文件的文件级条件编译里出现：重解析点探测与路径长度（Windows 走 Win32 属性查询，
// 其它平台走 lstat），避免把平台语义泄漏到调用方
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/stat.h>

    #include <cerrno>
#endif

/**
 *  路径规范化与越界判定实现。
 *
 *  三段职责：
 *  1. 规范化：normalize_absolute 只做词法与存在性判断（不跟随链接、不判形态）；
 *     normalize_relative 做形态校验（拒绝越界、非 POSIX 形态、Windows 保留名与 ADS）。
 *  2. 查询：is_under / last_segment_equals / parent_relative / file_name 只做字符串运算，不访问磁盘。
 *  3. 探测：check_reparse_points 逐段按 lstat 语义查询属性，不跟随链接。
 */

namespace ID::Project
{
    namespace
    {
        // 探测单个路径段：区分"普通"、"重解析点"、"不存在"与"查询失败"，让调用方决定停在哪
        enum class LinkProbe : std::uint8_t
        {
            Plain   = 0,    // 普通文件或目录
            Reparse = 1,    // 符号链接、junction 或挂载点
            Missing = 2,    // 该段不存在
            Failed  = 3,    // 属性查询失败（权限、非法路径等）
        };

        // 长名探测的缓冲上限：Windows 的扩展路径上限为 32767 个 UTF-16 单元，超过即放弃展开、保留原文
        constexpr std::size_t kMaxLongPathProbeLength = 32768;

        PathCheck make_failure(PathVerdict verdict, std::string detail)
        {
            PathCheck result;
            result.verdict = verdict;
            result.detail  = std::move(detail);
            return result;
        }

        PathCheck make_success(std::string text)
        {
            PathCheck result;
            result.verdict = PathVerdict::Ok;
            result.text    = std::move(text);
            return result;
        }

        // 形如 "C:" 的盘符前缀（相对路径里不允许出现）
        bool has_drive_letter(std::string_view text) noexcept
        {
            if (text.size() < 2 || text[1] != ':')
            {
                return false;
            }
            const char first = text[0];
            return (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
        }

        // 段比较按平台规则：Windows 折叠 ASCII 大小写，其它平台精确比较
        bool equal_segment(std::string_view left, std::string_view right) noexcept
        {
        #if defined(_WIN32)
            return ascii_iequals(left, right);
        #else
            return left == right;
        #endif
        }

        // 原生路径转 POSIX 分隔 UTF-8 文本：记录与诊断里统一用 '/' 分隔
        std::string to_generic_utf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();

            std::string result;
            result.reserve(text.size());
            for (const char8_t unit : text)
            {
                result.push_back(static_cast<char>(static_cast<unsigned char>(unit)));
            }
            return result;
        }

        LinkProbe probe_link(const std::filesystem::path& path) noexcept
        {
        #if defined(_WIN32)
            // GetFileAttributesW 不跟随符号链接：返回的是链接本身的属性
            const DWORD attributes = ::GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return LinkProbe::Missing;
                }
                return LinkProbe::Failed;
            }
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return LinkProbe::Reparse;
            }
            return LinkProbe::Plain;
        #else
            struct stat info = {};
            if (::lstat(path.c_str(), &info) != 0)
            {
                if (errno == ENOENT || errno == ENOTDIR)
                {
                    return LinkProbe::Missing;
                }
                return LinkProbe::Failed;
            }
            return S_ISLNK(info.st_mode) ? LinkProbe::Reparse : LinkProbe::Plain;
        #endif
        }

        #if defined(_WIN32)
            // Windows 保留设备名：CON/PRN/AUX/NUL 与 COM1–COM9、LPT1–LPT9；带扩展名（CON.txt）同样保留
            bool is_reserved_device_name(std::string_view segment)
            {
                const std::size_t      dot  = segment.find('.');
                const std::string_view base = dot == std::string_view::npos ? segment : segment.substr(0, dot);

                const std::string upper = ascii_upper(base);
                if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
                {
                    return true;
                }

                if (upper.size() == 4 && (upper.compare(0, 3, "COM") == 0 || upper.compare(0, 3, "LPT") == 0))
                {
                    const char digit = upper[3];
                    return digit >= '1' && digit <= '9';
                }
                return false;
            }

            // 单段的形态校验：ADS ':'、尾部空格或点、保留设备名
            PathCheck check_segment_form(std::string_view segment)
            {
                if (segment.find(':') != std::string_view::npos)
                {
                    return make_failure(PathVerdict::UnsupportedForm,
                                        "段含 ':'（Windows 备用数据流或盘符形态）: " + std::string(segment));
                }
                if (segment.back() == ' ' || segment.back() == '.')
                {
                    return make_failure(PathVerdict::UnsupportedForm,
                                        "段以空格或点结尾（Windows 会静默去尾）: " + std::string(segment));
                }
                if (is_reserved_device_name(segment))
                {
                    return make_failure(PathVerdict::UnsupportedForm,
                                        "段命中 Windows 保留设备名: " + std::string(segment));
                }
                return PathCheck{};
            }
        #else
            // 其它平台没有保留设备名、ADS 与尾部去尾语义：形态校验一律通过
            PathCheck check_segment_form(std::string_view)
            {
                return PathCheck{};
            }
        #endif
    } // namespace

    PathCheck normalize_absolute(std::string_view text, bool must_exist)
    {
        if (text.empty())
        {
            return make_failure(PathVerdict::NotAbsolute, "路径为空");
        }

        const std::filesystem::path input = to_native(text);
        if (!input.is_absolute())
        {
            return make_failure(PathVerdict::NotAbsolute, "路径不是绝对路径: " + std::string(text));
        }
        if (!is_path_length_ok(input))
        {
            return make_failure(PathVerdict::TooLong, "路径长度超出平台上限: " + std::string(text));
        }

        if (must_exist)
        {
            std::error_code error;
            const bool      is_existing = std::filesystem::exists(input, error);
            if (error)
            {
                return make_failure(PathVerdict::NotNormalizable, "检查路径存在性失败: " + error.message());
            }
            if (!is_existing)
            {
                return make_failure(PathVerdict::NotNormalizable, "路径不存在: " + std::string(text));
            }
        }

        // 词法规范化：折叠重复分隔符与 '.'，展开 '..'；绝对路径在根处截断，因此不会越出根
        std::filesystem::path rebuilt;
        for (const std::filesystem::path& part : input.lexically_normal())
        {
            if (part.empty() || part == ".")
            {
                continue;
            }
            rebuilt /= part;
        }

        if (!rebuilt.is_absolute())
        {
            return make_failure(PathVerdict::NotNormalizable, "规范化结果不是绝对路径: " + std::string(text));
        }
        return make_success(to_generic_utf8(rebuilt));
    }

    PathCheck normalize_relative(std::string_view text)
    {
        if (text.empty())
        {
            return make_failure(PathVerdict::UnsupportedForm, "相对路径为空");
        }
        if (text.front() == '/')
        {
            return make_failure(PathVerdict::UnsupportedForm, "相对路径不得以 '/' 开头: " + std::string(text));
        }
        if (text.find('\\') != std::string_view::npos)
        {
            return make_failure(PathVerdict::UnsupportedForm,
                                "相对路径必须用 POSIX '/' 分隔，不得含反斜杠: " + std::string(text));
        }
        if (has_drive_letter(text))
        {
            return make_failure(PathVerdict::UnsupportedForm, "相对路径不得含盘符: " + std::string(text));
        }

        std::string normalized;
        normalized.reserve(text.size());

        std::size_t begin = 0;
        for (;;)
        {
            const std::size_t      separator = text.find('/', begin);
            const std::size_t      end       = separator == std::string_view::npos ? text.size() : separator;
            const std::string_view segment   = text.substr(begin, end - begin);

            if (segment.empty())
            {
                return make_failure(PathVerdict::UnsupportedForm,
                                    "相对路径存在空段（'//' 或尾部 '/'）: " + std::string(text));
            }
            if (segment == "..")
            {
                return make_failure(PathVerdict::EscapesRoot, "相对路径不得含 '..': " + std::string(text));
            }
            if (segment != ".")
            {
                const PathCheck form = check_segment_form(segment);
                if (form.verdict != PathVerdict::Ok)
                {
                    return form;
                }

                if (!normalized.empty())
                {
                    normalized.push_back('/');
                }
                normalized.append(segment);
            }

            if (separator == std::string_view::npos)
            {
                break;
            }
            begin = separator + 1;
        }

        if (normalized.empty())
        {
            return make_failure(PathVerdict::UnsupportedForm,
                                "相对路径只含 '.' 段，规范化后为空: " + std::string(text));
        }
        return make_success(std::move(normalized));
    }

    bool is_under(std::string_view relative, std::string_view prefix) noexcept
    {
        if (prefix.empty())
        {
            return true;    // 空前缀表示项目根：任何已规范化的相对路径都在其中
        }
        if (relative.size() < prefix.size())
        {
            return false;
        }
        if (!equal_segment(relative.substr(0, prefix.size()), prefix))
        {
            return false;
        }
        if (relative.size() == prefix.size())
        {
            return true;
        }

        // 按段比较：前缀之后必须是分隔符，否则 "IDPluginX" 会被误判为在 "IDPlugin" 之内
        return relative[prefix.size()] == '/';
    }

    bool last_segment_equals(std::string_view relative, std::string_view name) noexcept
    {
        return equal_segment(file_name(relative), name);
    }

    std::string_view parent_relative(std::string_view relative) noexcept
    {
        const std::size_t separator = relative.rfind('/');
        if (separator == std::string_view::npos)
        {
            return std::string_view{};
        }
        return relative.substr(0, separator);
    }

    std::string_view file_name(std::string_view relative) noexcept
    {
        const std::size_t separator = relative.rfind('/');
        if (separator == std::string_view::npos)
        {
            return relative;
        }
        return relative.substr(separator + 1);
    }

    PathCheck check_reparse_points(const std::filesystem::path& absolute)
    {
        if (!absolute.is_absolute())
        {
            return make_failure(PathVerdict::UnsupportedForm, "路径不是绝对路径，无法逐段检查");
        }

        return check_reparse_points_under(absolute, absolute.root_path());
    }

    PathCheck check_reparse_points_under(const std::filesystem::path& absolute, const std::filesystem::path& root)
    {
        if (!absolute.is_absolute() || !root.is_absolute())
        {
            return make_failure(PathVerdict::UnsupportedForm, "路径不是绝对路径，无法逐段检查");
        }

        // 下界之外不判：项目根与工程根本身属于调用方环境，是否落在链路上不是越界
        const std::filesystem::path relative = absolute.lexically_relative(root);
        if (relative.empty())
        {
            return PathCheck{};   // 目标就是 root 本身：root 之内没有段可判
        }
        if (relative.is_absolute() || *relative.begin() == std::filesystem::path(".."))
        {
            return make_failure(PathVerdict::UnsupportedForm, "目标不在给定的根之内: " + to_utf8(absolute));
        }

        std::filesystem::path prefix = root;
        for (const std::filesystem::path& part : relative)
        {
            prefix /= part;

            switch (probe_link(prefix))
            {
                case LinkProbe::Plain:
                    continue;
                case LinkProbe::Missing:
                    // 现存前缀都已检查过，后段不存在也就不可能命中重解析点
                    return PathCheck{};
                case LinkProbe::Reparse:
                    return make_failure(PathVerdict::ReparsePoint,
                                        "路径段命中符号链接、junction 或挂载点: " + to_utf8(prefix));
                case LinkProbe::Failed:
                    return make_failure(PathVerdict::NotNormalizable, "无法读取路径段属性: " + to_utf8(prefix));
            }
        }
        return PathCheck{};
    }

    std::string expand_existing_prefixes(std::string_view text)
    {
    #if defined(_WIN32)
        if (text.empty())
        {
            return std::string(text);
        }

        const std::filesystem::path input = to_native(text);
        if (!input.is_absolute())
        {
            return std::string(text);
        }

        // 逐段累积到最深的已存在前缀：一旦某段不存在，更深的段也不可能存在（不跟随链接的语义与属性查询一致）
        std::filesystem::path existing = input.root_path();
        std::filesystem::path rest     = {};
        bool                  is_open  = true;
        for (const std::filesystem::path& part : input.relative_path())
        {
            if (is_open)
            {
                std::error_code error;
                const bool      is_found = std::filesystem::exists(existing / part, error);
                if (!error && is_found)
                {
                    existing /= part;
                    continue;
                }
                is_open = false;
            }
            rest /= part;
        }

        // GetLongPathNameW 要求路径存在：只对已存在前缀调用，失败时保持原文，不回退到短名。
        // 返回值含结尾空字符：长度小于缓冲即成功，否则说明缓冲不够、需要按倍数扩大（不是按返回值精确重设，
        // 否则返回值恰为 size - 1 时缓冲不增长，循环不终止）
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = ::GetLongPathNameW(existing.c_str(), buffer.data(),
                                                    static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return std::string(text);
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }
            if (buffer.size() >= kMaxLongPathProbeLength)
            {
                return std::string(text);   // 超出探测上限：保持原文，不无限增长缓冲
            }
            buffer.resize(buffer.size() * 2);
        }

        // 文本拼接而不是 operator/：后者在"后缀为空"时会留下尾分隔符，使同一目录出现两种形态
        std::string       result = to_generic_utf8(std::filesystem::path(buffer));
        const std::string tail   = to_generic_utf8(rest);
        if (!tail.empty())
        {
            if (result.empty() || result.back() != '/')
            {
                result.push_back('/');
            }
            result += tail;
        }
        return result;
    #else
        return std::string(text);    // 其它平台没有 8.3 别名：原样返回
    #endif
    }

    bool is_same_absolute_path(std::string_view left, std::string_view right)
    {
        if (left == right)
        {
            return true;
        }
    #if defined(_WIN32)
        // 文本命中即相等，不进磁盘查询；不相等时才展开短名——多数比较因此不产生额外系统调用
        if (ascii_iequals(left, right))
        {
            return true;
        }
        return ascii_iequals(expand_existing_prefixes(left), expand_existing_prefixes(right));
    #else
        return false;
    #endif
    }

    std::filesystem::path to_native(std::string_view utf8)
    {
        std::u8string text;
        text.reserve(utf8.size());
        for (const char unit : utf8)
        {
            text.push_back(static_cast<char8_t>(static_cast<unsigned char>(unit)));
        }
        return std::filesystem::path(text);
    }

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

    std::optional<std::string> to_relative(const std::filesystem::path& absolute,
                                           const std::filesystem::path& project_root)
    {
        const std::filesystem::path relative = absolute.lexically_relative(project_root);
        if (relative.empty())
        {
            return std::nullopt;    // 没有公共前缀（不同盘符或完全无关）
        }
        if (relative == ".")
        {
            return std::string();   // 等于项目根
        }

        std::string text;
        for (const std::filesystem::path& part : relative)
        {
            if (part == "..")
            {
                return std::nullopt;    // 越出项目根
            }
            if (!text.empty())
            {
                text.push_back('/');
            }
            text.append(to_utf8(part));
        }
        return text;
    }

    bool is_path_length_ok(const std::filesystem::path& absolute) noexcept
    {
    #if defined(_WIN32)
        const std::wstring& native = absolute.native();
        if (native.empty())
        {
            return true;
        }
        if (native.rfind(L"\\\\?\\", 0) == 0)
        {
            // 已带扩展前缀：上限就是 32767 个 UTF-16 单元
            return native.size() <= 32767;
        }

        // 需要 \\?\ 或 \\?\UNC\ 前缀时把前缀长度计入上限
        const bool        is_unc        = native.size() >= 2 && native[0] == L'\\' && native[1] == L'\\';
        const std::size_t prefix_length = is_unc ? 6 : 4;
        return native.size() + prefix_length <= 32767;
    #else
        (void)absolute;
        return true;    // 其它平台不判长度：由文件系统在打开时给出失败
    #endif
    }
} // namespace ID::Project
