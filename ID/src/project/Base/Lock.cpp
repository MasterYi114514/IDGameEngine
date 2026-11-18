#include "project/Base/Lock.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <system_error>
#include <utility>

#include "Core/Core.hpp"

#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Log/Log.hpp"

// 平台差异只在文件级条件编译里出现：独占创建、进程存活判定、启动时间与主机名取值
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/types.h>
    #include <unistd.h>

    #include <cerrno>
    #include <cstdlib>
#endif

/**
 *  锁协议实现。
 *
 *  锁文件内容为四行 `key=value`（pid / process_start / host / taken_at），键序固定。独占创建保证
 *  "同一时刻只有一个写入者"；陈旧判定只在能证明旧持有者已不存在时回收，回收顺序是
 *  改名 → 独占创建新锁 → 删除改名后的旧文件，任何失败都不删除锁文件本身。
 */

namespace ID::Project
{
    namespace
    {
        // 独占创建的三态：成功、已存在、I/O 失败；"已存在"是并发语义，不按失败处理
        enum class CreateOutcome : std::uint8_t
        {
            Created = 0,    // 新锁文件由本次创建
            Exists  = 1,    // 目标已存在（可能有活跃写入者）
            Failed  = 2,    // 无法创建
        };

        inline constexpr std::size_t kHostNameCapacity = 256;

        // TU 私有可变状态：陈旧锁文件的序号只在本进程内递增；工具是单线程离线工具，不要求并发安全
        std::uint32_t g_stale_sequence = 0;

        #if defined(_WIN32)
            // 写满整段内容：部分写入按失败处理，由调用方删除本次创建的文件
            bool write_all(void* raw_handle, std::string_view content) noexcept
            {
                const HANDLE handle = static_cast<HANDLE>(raw_handle);
                std::size_t  offset = 0;

                while (offset < content.size())
                {
                    const std::size_t remaining = content.size() - offset;
                    const DWORD       request =
                        static_cast<DWORD>(remaining > 0x10000000u ? 0x10000000u : remaining);

                    DWORD written = 0;
                    if (!::WriteFile(handle, content.data() + offset, request, &written, nullptr))
                    {
                        return false;
                    }
                    if (written == 0)
                    {
                        return false;
                    }
                    offset += written;
                }
                return true;
            }
        #else
            bool write_all(int file, std::string_view content) noexcept
            {
                std::size_t offset = 0;

                while (offset < content.size())
                {
                    const ssize_t written = ::write(file, content.data() + offset, content.size() - offset);
                    if (written <= 0)
                    {
                        return false;
                    }
                    offset += static_cast<std::size_t>(written);
                }
                return true;
            }
        #endif

        // 独占创建并立即写入内容：写内容失败时删除本次创建的文件，不留半份锁
        CreateOutcome create_exclusive_lock(const std::filesystem::path& path, std::string_view content) noexcept
        {
        #if defined(_WIN32)
            const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                const DWORD error = ::GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                {
                    return CreateOutcome::Exists;
                }
                return CreateOutcome::Failed;
            }

            bool is_ok = write_all(handle, content);
            if (is_ok && !::FlushFileBuffers(handle))
            {
                is_ok = false;
            }
            ::CloseHandle(handle);

            if (!is_ok)
            {
                ::DeleteFileW(path.c_str());
                return CreateOutcome::Failed;
            }
            return CreateOutcome::Created;
        #else
            const int file = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
            if (file < 0)
            {
                return errno == EEXIST ? CreateOutcome::Exists : CreateOutcome::Failed;
            }

            bool is_ok = write_all(file, content);
            if (is_ok && ::fsync(file) != 0)
            {
                is_ok = false;
            }
            ::close(file);

            if (!is_ok)
            {
                ::unlink(path.c_str());
                return CreateOutcome::Failed;
            }
            return CreateOutcome::Created;
        #endif
        }

        std::string serialize_owner(const LockOwner& owner)
        {
            std::string text;
            text += "pid=";
            text += to_decimal(owner.pid);
            text += '\n';
            text += "process_start=";
            text += to_decimal(owner.process_start);
            text += '\n';
            text += "host=";
            text += owner.host;
            text += '\n';
            text += "taken_at=";
            text += owner.taken_at;
            text += '\n';
            return text;
        }

        // 十进制解析：只接受数字，溢出返回 false
        bool parse_unsigned(std::string_view text, std::uint64_t& out) noexcept
        {
            if (text.empty())
            {
                return false;
            }

            std::uint64_t value = 0;
            for (const char unit : text)
            {
                if (unit < '0' || unit > '9')
                {
                    return false;
                }

                const std::uint64_t digit = static_cast<std::uint64_t>(unit - '0');
                if (value > (UINT64_MAX - digit) / 10)
                {
                    return false;
                }
                value = value * 10 + digit;
            }

            out = value;
            return true;
        }

        // 解析锁文件内容：pid 与 process_start 必须可解析，host 与 taken_at 缺省按空串处理
        bool parse_owner(std::string_view content, LockOwner& out) noexcept
        {
            LockOwner parsed;
            bool      has_pid           = false;
            bool      has_process_start = false;

            std::size_t begin = 0;
            for (;;)
            {
                const std::size_t      separator = content.find('\n', begin);
                const std::size_t      end = separator == std::string_view::npos ? content.size() : separator;
                const std::string_view line = trim_ascii_space(content.substr(begin, end - begin));

                if (!line.empty())
                {
                    const std::size_t assignment = line.find('=');
                    if (assignment == std::string_view::npos)
                    {
                        return false;
                    }

                    const std::string_view key   = trim_ascii_space(line.substr(0, assignment));
                    const std::string_view value = trim_ascii_space(line.substr(assignment + 1));

                    if (key == "pid")
                    {
                        std::uint64_t number = 0;
                        if (!parse_unsigned(value, number))
                        {
                            return false;
                        }
                        parsed.pid = static_cast<std::uint32_t>(number);
                        has_pid    = true;
                    }
                    else if (key == "process_start")
                    {
                        std::uint64_t number = 0;
                        if (!parse_unsigned(value, number))
                        {
                            return false;
                        }
                        parsed.process_start = number;
                        has_process_start    = true;
                    }
                    else if (key == "host")
                    {
                        parsed.host = std::string(value);
                    }
                    else if (key == "taken_at")
                    {
                        parsed.taken_at = std::string(value);
                    }
                }

                if (separator == std::string_view::npos)
                {
                    break;
                }
                begin = separator + 1;
            }

            if (!has_pid || !has_process_start)
            {
                return false;
            }

            out = std::move(parsed);
            return true;
        }

        // 陈旧锁的改名目标：<锁名>.stale.<pid>.<seq>，与锁文件同目录
        std::filesystem::path make_stale_path(const std::filesystem::path& lock_path)
        {
            ++g_stale_sequence;

            std::filesystem::path name = lock_path.filename();
            name += ".stale.";
            name += to_decimal(current_pid());
            name += ".";
            name += to_decimal(g_stale_sequence);

            return lock_path.parent_path() / name;
        }

        void remove_quietly(const std::filesystem::path& path) noexcept
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }

        // 陈旧判定的唯一入口：同主机且进程不存在或启动时间不符才算陈旧
        bool is_stale_owner(const LockOwner& owner)
        {
            if (!ascii_iequals(owner.host, current_host_name()))
            {
                return false;   // 其它主机无法判断存活，按活跃处理
            }
            return !is_process_alive(owner.pid, owner.process_start);
        }

        ProjectDiagnostic make_conflict_diagnostic(const LockOwner& owner, LockScope scope,
                                                   const std::filesystem::path& lock_path)
        {
            const std::string scope_text = scope == LockScope::Project ? "项目锁" : "登记锁";
            const std::string holder     = "pid=" + to_decimal(owner.pid) + "，host=" + owner.host +
                                           "，取锁时间=" + owner.taken_at;

            return ProjectDiagnostic{ProjectCategory::WriterConflict,
                                     scope_text,
                                     "没有其它写入者持有该锁",
                                     "锁已被持有：" + holder,
                                     to_utf8(lock_path),
                                     "等另一个写入结束或确认该进程已不存在后重跑同一命令"};
        }

        ProjectDiagnostic make_io_diagnostic(const std::string& subject, const std::string& expected,
                                             const std::string& actual)
        {
            return ProjectDiagnostic{ProjectCategory::WriteFailed,
                                     subject,
                                     expected,
                                     actual,
                                     std::string(to_identifier(ProjectCategory::WriteFailed)),
                                     "检查锁文件所在目录的权限与磁盘状态后重跑"};
        }

        // POSIX：读 /proc/<pid>/stat 的第 22 列（starttime）；取不到返回 0
        #if !defined(_WIN32)
            std::uint64_t read_process_start(std::uint32_t pid) noexcept
            {
                char path[64] = {};
                std::snprintf(path, sizeof(path), "/proc/%u/stat", pid);

                std::FILE* stream = std::fopen(path, "rb");
                if (stream == nullptr)
                {
                    return 0;
                }

                char              buffer[1024] = {};
                const std::size_t read         = std::fread(buffer, 1, sizeof(buffer) - 1, stream);
                std::fclose(stream);
                if (read == 0)
                {
                    return 0;
                }

                // comm 字段可能含空格与括号：从最后一个 ')' 之后开始数数字字段
                const char* cursor = std::strrchr(buffer, ')');
                if (cursor == nullptr)
                {
                    return 0;
                }
                ++cursor;

                // 跳过 state（单个字母），之后第 19 个数字字段就是 starttime
                while (*cursor == ' ')
                {
                    ++cursor;
                }
                while (*cursor != ' ' && *cursor != '\0')
                {
                    ++cursor;
                }

                for (int index = 0; index < 19; ++index)
                {
                    while (*cursor == ' ')
                    {
                        ++cursor;
                    }

                    char*             end   = nullptr;
                    const long long   value = std::strtoll(cursor, &end, 10);
                    if (end == cursor || value < 0)
                    {
                        return 0;
                    }
                    if (index == 18)
                    {
                        return static_cast<std::uint64_t>(value);
                    }
                    cursor = end;
                }
                return 0;
            }
        #endif
    } // namespace

    std::uint32_t current_pid() noexcept
    {
    #if defined(_WIN32)
        return static_cast<std::uint32_t>(::GetCurrentProcessId());
    #else
        return static_cast<std::uint32_t>(::getpid());
    #endif
    }

    std::uint64_t current_process_start() noexcept
    {
    #if defined(_WIN32)
        FILETIME creation = {}, exit_time = {}, kernel = {}, user = {};
        if (!::GetProcessTimes(::GetCurrentProcess(), &creation, &exit_time, &kernel, &user))
        {
            return 0;
        }
        return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    #else
        return read_process_start(current_pid());
    #endif
    }

    std::string current_host_name()
    {
    #if defined(_WIN32)
        wchar_t buffer[kHostNameCapacity] = {};
        DWORD   length                    = kHostNameCapacity;
        if (!::GetComputerNameW(buffer, &length))
        {
            return std::string();
        }

        const int size = ::WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return std::string();
        }

        std::string result(static_cast<std::size_t>(size), '\0');
        if (::WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), result.data(), size, nullptr,
                                  nullptr) != size)
        {
            return std::string();
        }
        return result;
    #else
        char buffer[kHostNameCapacity] = {};
        if (::gethostname(buffer, sizeof(buffer) - 1) != 0)
        {
            return std::string();
        }
        return std::string(buffer);
    #endif
    }

    bool is_process_alive(std::uint32_t pid, std::uint64_t process_start) noexcept
    {
        if (pid == 0)
        {
            return false;
        }

    #if defined(_WIN32)
        const HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (handle == nullptr)
        {
            // 无权查询不等于进程不存在：无法证明陈旧时按存活处理，不抢占他人锁
            return ::GetLastError() == ERROR_ACCESS_DENIED;
        }

        // 已终止的进程在最后一个句柄关闭前仍可被 OpenProcess 打开，GetProcessTimes 也会给出原来的创建时间：
        // 只看创建时间会把崩溃（Abort）残留的锁判成"活着"。先看退出码：非 STILL_ACTIVE 即已终止
        DWORD             exit_code = STILL_ACTIVE;
        const bool        is_exited = ::GetExitCodeProcess(handle, &exit_code) != 0 && exit_code != STILL_ACTIVE;
        FILETIME          creation = {}, exit_time = {}, kernel = {}, user = {};
        const bool        is_queried = ::GetProcessTimes(handle, &creation, &exit_time, &kernel, &user) != 0;
        ::CloseHandle(handle);
        if (is_exited)
        {
            return false;
        }
        if (!is_queried)
        {
            return true;
        }

        const std::uint64_t start = (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
        return start == process_start;
    #else
        if (::kill(static_cast<pid_t>(pid), 0) != 0)
        {
            return errno == EPERM;   // EPERM：进程存在但无权发信号
        }

        const std::uint64_t start = read_process_start(pid);
        if (start == 0)
        {
            return true;    // /proc 不可读：无法证伪，按存活处理
        }
        return start == process_start;
    #endif
    }

    std::string iso8601_now_with_offset()
    {
        const std::time_t now = std::time(nullptr);

        std::tm local_time = {};
        std::tm utc_time   = {};
    #if defined(_WIN32)
        if (::localtime_s(&local_time, &now) != 0 || ::gmtime_s(&utc_time, &now) != 0)
        {
            return std::string();
        }
    #else
        if (::localtime_r(&now, &local_time) == nullptr || ::gmtime_r(&now, &utc_time) == nullptr)
        {
            return std::string();
        }
    #endif

        // 偏移量 = 把本地时间当 UTC 解释与真实 UTC 之差；与夏令时无关，因为两者取自同一时刻
    #if defined(_WIN32)
        const std::int64_t local_seconds = static_cast<std::int64_t>(::_mkgmtime(&local_time));
        const std::int64_t utc_seconds   = static_cast<std::int64_t>(::_mkgmtime(&utc_time));
    #else
        const std::int64_t local_seconds = static_cast<std::int64_t>(::timegm(&local_time));
        const std::int64_t utc_seconds   = static_cast<std::int64_t>(::timegm(&utc_time));
    #endif
        std::int64_t offset_seconds = local_seconds - utc_seconds;

        const char sign = offset_seconds < 0 ? '-' : '+';
        if (offset_seconds < 0)
        {
            offset_seconds = -offset_seconds;
        }
        const std::int64_t offset_hours   = offset_seconds / 3600;
        const std::int64_t offset_minutes = (offset_seconds % 3600) / 60;

        // 分两步格式化：单次格式化的参数个数保持很小，避免踩到工具链的长参数格式化缺陷
        char date[16] = {};
        std::snprintf(date, sizeof(date), "%04d-%02d-%02dT", local_time.tm_year + 1900, local_time.tm_mon + 1,
                      local_time.tm_mday);

        char clock[16] = {};
        std::snprintf(clock, sizeof(clock), "%02d:%02d:%02d", local_time.tm_hour, local_time.tm_min, local_time.tm_sec);

        char offset[16] = {};
        std::snprintf(offset, sizeof(offset), "%c%02d:%02d", sign, static_cast<int>(offset_hours),
                      static_cast<int>(offset_minutes));

        std::string result(date);
        result += clock;
        result += offset;
        return result;
    }

    ProjectLock::~ProjectLock()
    {
        release();
    }

    ProjectLock::ProjectLock(ProjectLock&& other) noexcept
        : m_path(std::move(other.m_path))
        , m_is_held(other.m_is_held)
    {
        other.m_path.clear();
        other.m_is_held = false;
    }

    ProjectLock& ProjectLock::operator=(ProjectLock&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        release();

        m_path          = std::move(other.m_path);
        m_is_held       = other.m_is_held;
        other.m_path.clear();
        other.m_is_held = false;
        return *this;
    }

    ProjectLock::AcquireResult ProjectLock::acquire(const std::filesystem::path& path, LockScope scope)
    {
        AcquireResult result;

        const std::string subject = scope == LockScope::Project ? "项目锁" : "登记锁";

        if (path.empty())
        {
            result.verdict    = LockVerdict::IoFailure;
            result.diagnostic = make_io_diagnostic(subject, "一个非空的锁文件路径", "锁文件路径为空");
            return result;
        }

        if (m_is_held)
        {
            if (m_path == path)
            {
                result.verdict = LockVerdict::Acquired;   // 重复取得同一把锁：不重复创建
                return result;
            }

            result.verdict    = LockVerdict::IoFailure;
            result.diagnostic = make_io_diagnostic(subject, "尚未持有其它锁文件",
                                                   "已持有 " + to_utf8(m_path) + "，不能同时持有两个锁文件");
            return result;
        }

        const LockOwner   owner   = LockOwner{current_pid(), current_process_start(), current_host_name(),
                                              iso8601_now_with_offset()};
        const std::string content = serialize_owner(owner);

        const CreateOutcome created = create_exclusive_lock(path, content);
        if (created == CreateOutcome::Created)
        {
            m_path    = path;
            m_is_held = true;
            result.verdict = LockVerdict::Acquired;
            return result;
        }
        if (created == CreateOutcome::Failed)
        {
            result.verdict    = LockVerdict::IoFailure;
            result.diagnostic = make_io_diagnostic(subject, "独占创建锁文件",
                                                   "创建锁文件失败：" + to_utf8(path));
            return result;
        }

        // 已有锁文件：读取持有者信息判定陈旧
        const FileRead existing = read_file(path);
        if (existing.status == ReadStatus::Failed)
        {
            result.verdict    = LockVerdict::IoFailure;
            result.diagnostic = make_io_diagnostic(subject, "能读取已有锁文件",
                                                   "读取锁文件失败：" + to_utf8(path));
            return result;
        }
        if (existing.status == ReadStatus::NotFound)
        {
            // 竞争窗口：另一个进程刚释放锁，重试一次独占创建
            const CreateOutcome retried = create_exclusive_lock(path, content);
            if (retried == CreateOutcome::Created)
            {
                m_path    = path;
                m_is_held = true;
                result.verdict = LockVerdict::Acquired;
                return result;
            }

            result.verdict = LockVerdict::WriterConflict;
            result.diagnostic = ProjectDiagnostic{ProjectCategory::WriterConflict,
                                                  subject,
                                                  "没有其它写入者持有该锁",
                                                  "锁文件在判定期间被其它进程重新创建",
                                                  std::string(to_identifier(ProjectCategory::WriterConflict)),
                                                  "稍后重跑同一命令"};
            return result;
        }

        LockOwner other;
        if (!parse_owner(existing.bytes, other))
        {
            // 内容无法解析时不能证明陈旧：不删除他人文件，按冲突处理
            result.verdict = LockVerdict::WriterConflict;
            result.diagnostic = ProjectDiagnostic{ProjectCategory::WriterConflict,
                                                  subject,
                                                  "锁文件内容可解析（四行 key=value）",
                                                  "锁文件内容无法解析：" + to_utf8(path),
                                                  std::string(to_identifier(ProjectCategory::WriterConflict)),
                                                  "确认没有其它 ID 进程在运行后手工删除该锁文件并重跑"};
            return result;
        }

        if (!is_stale_owner(other))
        {
            result.verdict    = LockVerdict::WriterConflict;
            result.diagnostic = make_conflict_diagnostic(other, scope, path);
            return result;
        }

        // 陈旧回收：先改名，再独占创建新锁，最后删除改名后的旧文件
        const std::filesystem::path stale_path = make_stale_path(path);
        std::error_code             error;
        std::filesystem::rename(path, stale_path, error);
        if (error)
        {
            result.verdict    = LockVerdict::IoFailure;
            result.diagnostic = make_io_diagnostic(subject, "把陈旧锁文件改名",
                                                   "改名失败（" + error.message() + "）：" + to_utf8(path));
            return result;
        }

        const CreateOutcome recovered = create_exclusive_lock(path, content);
        if (recovered != CreateOutcome::Created)
        {
            remove_quietly(stale_path);

            if (recovered == CreateOutcome::Exists)
            {
                result.verdict = LockVerdict::WriterConflict;
                result.diagnostic = ProjectDiagnostic{ProjectCategory::WriterConflict,
                                                      subject,
                                                      "没有其它写入者持有该锁",
                                                      "另一进程在回收陈旧锁的窗口内先取得了锁",
                                                      std::string(to_identifier(ProjectCategory::WriterConflict)),
                                                      "稍后重跑同一命令"};
                return result;
            }

            result.verdict    = LockVerdict::IoFailure;
            result.diagnostic = make_io_diagnostic(subject, "在回收陈旧锁后独占创建新锁",
                                                   "创建锁文件失败：" + to_utf8(path));
            return result;
        }

        remove_quietly(stale_path);

        m_path    = path;
        m_is_held = true;
        result.verdict             = LockVerdict::Acquired;
        result.is_stale_recovered  = true;

        // 回收是 warn 级普通信息：不新增诊断类别，也不改变退出码。
        // 日志器可能尚未初始化（例如直接链接 IDCore 的消费者），此处显式判空后再用宏
        if (IDProject_logger != nullptr)
        {
            IDPROJECT_LOG_WARN("ProjectLock::acquire: 回收了陈旧锁文件: {}", to_utf8(stale_path));
        }
        return result;
    }

    void ProjectLock::release() noexcept
    {
        if (!m_is_held)
        {
            return;
        }

        // 先清状态再删文件：删除失败也不留下"以为还持有"的对象
        const std::filesystem::path held = m_path;
        m_path.clear();
        m_is_held = false;
        remove_quietly(held);
    }

    bool ProjectLock::is_held() const noexcept
    {
        return m_is_held;
    }

    const std::filesystem::path& ProjectLock::path() const noexcept
    {
        return m_path;
    }
} // namespace ID::Project
