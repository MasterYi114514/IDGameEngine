#include "project/Base/FileIo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "Core/Core.hpp"

#include "project/Base/Lock.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"

// 平台差异只在文件级条件编译里出现：原子替换与"flush 之后同步到磁盘"是仅有的两处
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>

    #include <io.h>
#else
    #include <unistd.h>

    #include <cerrno>
#endif

/**
 *  文件读写机制实现。
 *
 *  写入协议（memory.md 第 3 节）：先在内存里成形（调用方已完成）→ 写同目录临时文件 → flush →
 *  原子替换目标。临时文件名带进程号与进程内序号，替换失败或校验失败时只删除本进程刚创建的那个
 *  临时文件，原目标保持不变。
 *
 *  磁盘同步：is_flush_required 为真时在替换前把临时文件同步到磁盘（Windows FlushFileBuffers 的
 *  等价物 `_commit`，POSIX `fsync`），缩小断电窗口；不做目录级同步，也不承诺掉电级持久。
 */

namespace ID::Project
{
    namespace
    {
        inline constexpr std::size_t kCopyBufferBytes = 64u << 10;     // 复制与校验的流式缓冲

        // TU 私有可变状态：临时文件序号只在本进程内递增；工具是单线程离线工具，不要求并发安全
        std::uint32_t g_temporary_sequence = 0;

        std::string platform_error_text()
        {
        #if defined(_WIN32)
            return "错误码 " + to_decimal(static_cast<std::uint64_t>(::GetLastError()));
        #else
            const int error = errno;
            return "errno " + to_signed_decimal(static_cast<std::int64_t>(error)) + "（" + std::strerror(error) + "）";
        #endif
        }

        // 把"目标不存在"从查询错误里剥出来：不同标准库实现对 ENOENT 的报告口径不一致
        // （libstdc++ 在 Windows 上会把状态查询的 ENOENT 报成错误码），而调用方要的是"不存在"这个事实，
        // 不能让"文件不存在"与"权限不足"共用同一个 is_ok 为假的分支
        bool is_missing_error(const std::error_code& error) noexcept
        {
            return error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
        }

        ProjectDiagnostic make_io_diagnostic(std::string subject, std::string expected, std::string actual,
                                             std::string next_action)
        {
            return ProjectDiagnostic{ProjectCategory::WriteFailed, std::move(subject), std::move(expected),
                                     std::move(actual), {}, std::move(next_action)};
        }

        std::FILE* open_for_write(const std::filesystem::path& path) noexcept
        {
        #if defined(_WIN32)
            return ::_wfopen(path.c_str(), L"wb");
        #else
            return std::fopen(path.c_str(), "wb");
        #endif
        }

        std::FILE* open_for_read(const std::filesystem::path& path) noexcept
        {
        #if defined(_WIN32)
            return ::_wfopen(path.c_str(), L"rb");
        #else
            return std::fopen(path.c_str(), "rb");
        #endif
        }

        // flush 之后再同步到磁盘：缩小"替换成功但内容还在缓存里"的窗口
        bool sync_to_disk(std::FILE* stream) noexcept
        {
        #if defined(_WIN32)
            return ::_commit(::_fileno(stream)) == 0;
        #else
            return ::fsync(::fileno(stream)) == 0;
        #endif
        }

        // 原子替换：成功之前不删除原目标；两者都在同一目录，因此不会跨卷
        bool replace_file(const std::filesystem::path& source, const std::filesystem::path& target) noexcept
        {
        #if defined(_WIN32)
            return ::MoveFileExW(source.c_str(), target.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        #else
            return ::rename(source.c_str(), target.c_str()) == 0;
        #endif
        }

        std::filesystem::path make_temp_path(const std::filesystem::path& target)
        {
            ++g_temporary_sequence;

            std::filesystem::path name = target.filename();
            name += ".idtmp.";
            name += to_decimal(current_pid());
            name += ".";
            name += to_decimal(g_temporary_sequence);

            return target.parent_path() / name;
        }

        void cleanup_temporary(const std::filesystem::path& temporary) noexcept
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }

        // 整段写入 + flush（可选同步到磁盘）+ 关闭；detail 带平台错误码，便于定位
        bool write_whole_file(const std::filesystem::path& path, std::string_view bytes, bool is_flush_required,
                              std::string& detail)
        {
            std::FILE* stream = open_for_write(path);
            if (stream == nullptr)
            {
                detail = "无法创建临时文件（" + platform_error_text() + "）";
                return false;
            }

            bool is_ok = true;
            if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), stream) != bytes.size())
            {
                detail = "写入临时文件失败（" + platform_error_text() + "）";
                is_ok  = false;
            }
            if (is_ok && std::fflush(stream) != 0)
            {
                detail = "刷新临时文件失败（" + platform_error_text() + "）";
                is_ok  = false;
            }
            if (is_ok && is_flush_required && !sync_to_disk(stream))
            {
                detail = "同步临时文件到磁盘失败（" + platform_error_text() + "）";
                is_ok  = false;
            }
            if (std::fclose(stream) != 0 && is_ok)
            {
                detail = "关闭临时文件失败（" + platform_error_text() + "）";
                is_ok  = false;
            }
            return is_ok;
        }

        std::string subject_of(const WriteRequest& request)
        {
            if (!request.subject.empty())
            {
                return request.subject;
            }
            return to_utf8(request.target);
        }
    } // namespace

    FileRead read_file(const std::filesystem::path& path)
    {
        FileRead result;

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            std::error_code error;
            const bool      is_existing = std::filesystem::exists(path, error);
            result.status = (is_existing && !error) ? ReadStatus::Failed : ReadStatus::NotFound;
            return result;
        }

        result.bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        result.status = stream.bad() ? ReadStatus::Failed : ReadStatus::Found;
        return result;
    }

    bool path_exists(const std::filesystem::path& path, bool& is_ok) noexcept
    {
        // 不跟随链接：链接本身存在即为存在（悬垂链接也算存在），是否越界由重解析点检查判定
        std::error_code                   error;
        const std::filesystem::file_status info = std::filesystem::symlink_status(path, error);
        if (error && is_missing_error(error))
        {
            is_ok = true;       // "不存在"是查询成功的结论，不是查询失败
            return false;
        }
        is_ok = !error;
        return !error && info.type() != std::filesystem::file_type::not_found;
    }

    bool is_regular_file(const std::filesystem::path& path, bool& is_ok) noexcept
    {
        std::error_code                   error;
        const std::filesystem::file_status info = std::filesystem::symlink_status(path, error);
        if (error && is_missing_error(error))
        {
            is_ok = true;
            return false;
        }
        is_ok = !error;
        return !error && info.type() == std::filesystem::file_type::regular;
    }

    bool is_directory(const std::filesystem::path& path, bool& is_ok) noexcept
    {
        std::error_code                   error;
        const std::filesystem::file_status info = std::filesystem::symlink_status(path, error);
        if (error && is_missing_error(error))
        {
            is_ok = true;
            return false;
        }
        is_ok = !error;
        return !error && info.type() == std::filesystem::file_type::directory;
    }

    bool is_empty_directory(const std::filesystem::path& path, bool& is_ok) noexcept
    {
        std::error_code                   error;
        const std::filesystem::file_status info = std::filesystem::symlink_status(path, error);
        if (error && !is_missing_error(error))
        {
            is_ok = false;
            return false;
        }
        if (error || info.type() != std::filesystem::file_type::directory)
        {
            is_ok = true;
            return false;   // 不存在、是链接或不是目录：不算"空目录"，也不当成查询失败
        }

        const bool is_empty = std::filesystem::is_empty(path, error);
        if (error && !is_missing_error(error))
        {
            is_ok = false;
            return false;
        }
        is_ok = true;
        return is_empty;
    }

    DirectoryList list_directory(const std::filesystem::path& path)
    {
        DirectoryList result;

        std::error_code                          error;
        std::filesystem::directory_iterator      iterator(path, error);
        if (error)
        {
            return result;
        }

        const std::filesystem::directory_iterator end;
        while (iterator != end)
        {
            result.names.push_back(to_utf8(iterator->path().filename()));

            iterator.increment(error);
            if (error)
            {
                // 列举中途失败：不返回半份清单，避免调用方据不完整结果判定"目录为空"
                result.names.clear();
                return result;
            }
        }

        std::sort(result.names.begin(), result.names.end());
        result.is_ok = true;
        return result;
    }

    WriteOutcome write_file_atomic(const WriteRequest& request)
    {
        WriteOutcome outcome;

        if (request.target.empty())
        {
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "一个非空的目标路径", "目标路径为空",
                                                    "检查计划里的目标路径");
            return outcome;
        }

        // 注入点：未武装时只做一次已缓存的布尔判断，生产路径行为不变
        if (request.point != InjectPoint::None && hit_inject_point(request.point))
        {
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "完成本次写入",
                                                    "注入点被触发，按失败处理（" +
                                                        std::string(to_identifier(request.point)) + "）",
                                                    "去掉 ID_TEST_INJECT 后重跑同一命令");
            return outcome;
        }

        if (request.is_skip_if_identical)
        {
            const FileRead existing = read_file(request.target);
            if (existing.status == ReadStatus::Failed)
            {
                outcome.diagnostic = make_io_diagnostic(subject_of(request), "能读取现有目标以比较内容",
                                                        "读取现有目标失败", "关闭占用该文件的进程或修正权限后重跑");
                return outcome;
            }
            if (existing.status == ReadStatus::Found && existing.bytes == request.bytes)
            {
                // 幂等跳过：内容相同不重写，时间戳保持不变
                outcome.is_ok = true;
                return outcome;
            }
        }

        const std::filesystem::path temporary = make_temp_path(request.target);

        std::string failure_detail;
        if (!write_whole_file(temporary, request.bytes, request.is_flush_required, failure_detail))
        {
            cleanup_temporary(temporary);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "写入同目录临时文件并 flush",
                                                    failure_detail, "检查目标目录的可写权限与磁盘空间后重跑");
            return outcome;
        }

        if (!replace_file(temporary, request.target))
        {
            const std::string detail = "原子替换失败（" + platform_error_text() + "）";
            cleanup_temporary(temporary);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "原子替换目标文件", detail,
                                                    "关闭占用目标文件的进程后重跑");
            return outcome;
        }

        outcome.is_ok      = true;
        outcome.is_written = true;
        return outcome;
    }

    RemoveOutcome remove_file(const std::filesystem::path& path)
    {
        RemoveOutcome outcome;

        // 用 symlink_status（不跟随链接）判断类型与存在性：链接本身也是可删除的文件，
        // 而 exists／is_regular_file 会跟随链接，悬垂链接在它们眼里表现为"不存在"，直接用会让链接残留
        std::error_code                   error;
        const std::filesystem::file_status info = std::filesystem::symlink_status(path, error);
        if (error && !is_missing_error(error))
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "能查询目标属性",
                                                    "查询目标属性失败（" + error.message() + "）",
                                                    "检查路径与权限后重跑");
            return outcome;
        }
        if (!error && info.type() == std::filesystem::file_type::not_found)
        {
            outcome.is_ok = true;   // 不存在视为成功（is_removed 为假）
            return outcome;
        }
        if (!error && info.type() == std::filesystem::file_type::directory)
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "一个普通文件", "目标是目录",
                                                    "确认该路径的类型；本工具不递归删除目录");
            return outcome;
        }

        std::error_code remove_error;
        const bool      is_removed = std::filesystem::remove(path, remove_error);
        if (remove_error && !is_missing_error(remove_error))
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "删除该文件",
                                                    "删除失败（" + remove_error.message() + "）",
                                                    "关闭占用该文件的进程或修正权限后重跑");
            return outcome;
        }

        outcome.is_ok      = true;
        outcome.is_removed = is_removed;
        return outcome;
    }

    RemoveOutcome remove_empty_directory(const std::filesystem::path& path)
    {
        RemoveOutcome outcome;

        // 与 remove_file 同口径：不跟随链接。指向目录的链接按"不是目录"处理，
        // 删除阶段不跟随链接，避免删掉链接目标的目录内容
        std::error_code                   error;
        const std::filesystem::file_status info = std::filesystem::symlink_status(path, error);
        if (error && !is_missing_error(error))
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "能查询目录属性",
                                                    "查询目录属性失败（" + error.message() + "）",
                                                    "检查路径与权限后重跑");
            return outcome;
        }
        if (!error && info.type() == std::filesystem::file_type::not_found)
        {
            outcome.is_ok = true;   // 不存在视为成功（is_removed 为假）
            return outcome;
        }
        if (!error && info.type() != std::filesystem::file_type::directory)
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "一个空目录", "目标存在但不是目录",
                                                    "确认该路径的类型后重跑");
            return outcome;
        }

        bool       is_ok    = false;
        const bool is_empty = is_empty_directory(path, is_ok);
        if (!is_ok)
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "能判断目录是否为空", "判断目录是否为空失败",
                                                    "检查目录权限后重跑");
            return outcome;
        }
        if (!is_empty)
        {
            // 语义上不属清理阻断：这不是条目判定，而是"要求删空目录但目录非空"
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "一个空目录", "目录非空",
                                                    "先处理目录内条目；本工具不递归删除目录");
            return outcome;
        }

        std::error_code remove_error;
        const bool      is_removed = std::filesystem::remove(path, remove_error);
        if (remove_error && !is_missing_error(remove_error))
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "删除该空目录",
                                                    "删除失败（" + remove_error.message() + "）",
                                                    "检查目录权限与占用后重跑");
            return outcome;
        }

        outcome.is_ok      = true;
        outcome.is_removed = is_removed;
        return outcome;
    }

    RemoveOutcome create_directories(const std::filesystem::path& path)
    {
        RemoveOutcome outcome;

        if (path.empty())
        {
            outcome.diagnostic = make_io_diagnostic(std::string(), "一个非空的目录路径", "目录路径为空",
                                                    "检查计划里的目录路径");
            return outcome;
        }

        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error)
        {
            outcome.diagnostic = make_io_diagnostic(to_utf8(path), "创建目录及其缺失的父目录",
                                                    "创建失败（" + error.message() + "）",
                                                    "检查父目录权限与磁盘空间后重跑");
            return outcome;
        }

        outcome.is_ok = true;
        return outcome;
    }

    CopyOutcome copy_file_atomic(const std::filesystem::path& source, const WriteRequest& request)
    {
        CopyOutcome outcome;

        if (request.target.empty())
        {
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "一个非空的目标路径", "目标路径为空",
                                                    "检查计划里的目标路径");
            return outcome;
        }

        if (request.point != InjectPoint::None && hit_inject_point(request.point))
        {
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "完成本次复制",
                                                    "注入点被触发，按失败处理（" +
                                                        std::string(to_identifier(request.point)) + "）",
                                                    "去掉 ID_TEST_INJECT 后重跑同一命令");
            return outcome;
        }

        std::FILE* input = open_for_read(source);
        if (input == nullptr)
        {
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "能读取源文件",
                                                    "打开源文件失败（" + platform_error_text() + "）",
                                                    "确认源文件存在且可读后重跑");
            return outcome;
        }

        const std::filesystem::path temporary = make_temp_path(request.target);
        std::FILE* output = open_for_write(temporary);
        if (output == nullptr)
        {
            const std::string detail = "无法创建临时文件（" + platform_error_text() + "）";
            std::fclose(input);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "写入同目录临时文件", detail,
                                                    "检查目标目录的可写权限与磁盘空间后重跑");
            return outcome;
        }

        // 缓冲用 nothrow 分配：分配失败通过返回值表达，不把复制变成终止路径
        const std::unique_ptr<std::uint8_t[]> buffer(new (std::nothrow) std::uint8_t[kCopyBufferBytes]);
        if (!buffer)
        {
            std::fclose(input);
            std::fclose(output);
            cleanup_temporary(temporary);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "分配复制缓冲", "分配 64 KiB 缓冲失败",
                                                    "释放内存后重跑");
            return outcome;
        }

        Sha256      hasher;
        bool        is_ok        = true;
        std::string failure_detail;

        for (;;)
        {
            const std::size_t read = std::fread(buffer.get(), 1, kCopyBufferBytes, input);
            if (read > 0)
            {
                hasher.update(std::span<const std::uint8_t>(buffer.get(), read));
                if (std::fwrite(buffer.get(), 1, read, output) != read)
                {
                    failure_detail = "写入临时文件失败（" + platform_error_text() + "）";
                    is_ok          = false;
                    break;
                }
            }
            if (read < kCopyBufferBytes)
            {
                if (std::ferror(input) != 0)
                {
                    failure_detail = "读取源文件失败（" + platform_error_text() + "）";
                    is_ok          = false;
                }
                break;
            }
        }

        if (is_ok && std::fflush(output) != 0)
        {
            failure_detail = "刷新临时文件失败（" + platform_error_text() + "）";
            is_ok          = false;
        }
        if (is_ok && request.is_flush_required && !sync_to_disk(output))
        {
            failure_detail = "同步临时文件到磁盘失败（" + platform_error_text() + "）";
            is_ok          = false;
        }

        std::fclose(input);
        if (std::fclose(output) != 0 && is_ok)
        {
            failure_detail = "关闭临时文件失败（" + platform_error_text() + "）";
            is_ok          = false;
        }

        if (!is_ok)
        {
            cleanup_temporary(temporary);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "完成复制并 flush", failure_detail,
                                                    "检查源文件可读性、目标目录写权限与磁盘空间后重跑");
            return outcome;
        }

        outcome.digest = hasher.finish();

        // 回读校验：拿写出的临时文件与源摘要比对，失败时目标保持原状
        bool         is_read_ok = false;
        const Digest written    = digest_file(temporary, is_read_ok);
        if (!is_read_ok || written.bytes != outcome.digest.bytes)
        {
            cleanup_temporary(temporary);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "写出内容的摘要与源摘要一致",
                                                    is_read_ok ? "回读摘要不符：" + written.to_hex() + "，期望 " +
                                                                     outcome.digest.to_hex()
                                                               : "回读临时文件失败",
                                                    "重跑复制；持续失败时检查磁盘与杀毒软件干预");
            outcome.digest = Digest{};
            return outcome;
        }

        if (request.is_skip_if_identical)
        {
            bool         is_target_ok = false;
            const Digest existing     = digest_file(request.target, is_target_ok);
            if (is_target_ok && existing.bytes == outcome.digest.bytes)
            {
                // 目标已与源内容一致：不替换，时间戳保持不变
                cleanup_temporary(temporary);
                outcome.is_ok = true;
                return outcome;
            }
        }

        if (!replace_file(temporary, request.target))
        {
            const std::string detail = "原子替换失败（" + platform_error_text() + "）";
            cleanup_temporary(temporary);
            outcome.diagnostic = make_io_diagnostic(subject_of(request), "原子替换目标文件", detail,
                                                    "关闭占用目标文件的进程后重跑");
            outcome.digest = Digest{};
            return outcome;
        }

        outcome.is_ok      = true;
        outcome.is_written = true;
        return outcome;
    }

    FileStat stat_file(const std::filesystem::path& path)
    {
        FileStat result;

        bool         is_read_ok = false;
        const Digest digest     = digest_file(path, is_read_ok);
        if (!is_read_ok)
        {
            return result;
        }

        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error)
        {
            return result;
        }

        const std::filesystem::file_time_type file_time = std::filesystem::last_write_time(path, error);
        if (error)
        {
            return result;
        }

        // 文件时钟转系统时钟：mtime 只作信息字段，不参与构建成功判定（spec §12.5）
        const auto system_time = std::chrono::file_clock::to_sys(file_time);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(system_time.time_since_epoch());

        result.is_ok                 = true;
        result.digest                = digest;
        result.mtime_unix_seconds    = static_cast<std::int64_t>(seconds.count());
        result.size_bytes            = static_cast<std::uint64_t>(size);
        return result;
    }
} // namespace ID::Project
