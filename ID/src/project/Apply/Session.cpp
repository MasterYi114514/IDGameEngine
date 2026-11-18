#include "project/Apply/Session.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "project/Base/Digest.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  锁序列、最小 bootstrap 与锁内基线读取。
 *
 *  bootstrap 只创建 `IDGenerated/` 与锁文件；`before/`、`backup/` 由各自的写入点按需创建，`log/` 只由
 *  构建产生。创建或取锁失败时逆序删除本次新建的空目录——不留半个骨架，也不删任何非本次创建的文件。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kGeneratedDirectory = "IDGenerated";

        // attempt 序号：同一进程内递增，保证 attempt 标识不重复
        std::uint64_t g_attempt_sequence = 0;

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        // 记录文件读取：不存在记 absent，读取失败按 record_invalid
        bool read_record_snapshot(const std::filesystem::path& path, std::string_view subject,
                                  RecordSnapshot& snapshot, std::optional<ProjectDiagnostic>& diagnostic)
        {
            const FileRead read = read_file(path);
            if (read.status == ReadStatus::Failed)
            {
                diagnostic = make_diagnostic(ProjectCategory::RecordInvalid, std::string(subject),
                                             "能读取该记录文件", "打开或读取失败", std::string(subject),
                                             "确认记录文件未被其它进程占用或有读权限后重跑");
                return false;
            }
            if (read.status == ReadStatus::NotFound)
            {
                snapshot = RecordSnapshot{};
                return true;
            }

            snapshot.is_present = true;
            snapshot.bytes      = read.bytes;
            snapshot.digest     = digest_text(read.bytes).to_hex();
            return true;
        }
    } // namespace

    Session::~Session()
    {
        close();
    }

    Session::OpenResult Session::open(const std::filesystem::path& project_root, bool is_create_allowed)
    {
        OpenResult result;
        if (m_is_open)
        {
            result.is_ok = true;
            return result;
        }

        m_project_root = project_root;

        // 最小 bootstrap：只创建 IDGenerated/（以及项目根）；记录最初的状态以便失败时只清理本次新建的
        if (is_create_allowed)
        {
            const std::filesystem::path generated_root = project_root / to_native(kGeneratedDirectory);

            bool is_query_ok = false;
            const bool is_root_existing = path_exists(project_root, is_query_ok);
            if (!is_query_ok)
            {
                result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, to_utf8(project_root),
                                                    "能查询项目根属性", "属性查询失败", to_utf8(project_root),
                                                    "修正目录权限后重跑");
                return result;
            }
            if (!is_root_existing)
            {
                const RemoveOutcome created = ID::Project::create_directories(project_root);
                if (!created.is_ok)
                {
                    if (created.diagnostic.has_value())
                    {
                        result.diagnostic = created.diagnostic;
                    }
                    return result;
                }
                m_created_directories.push_back(project_root);
                result.created_directories = true;
            }

            bool is_generated_query_ok = false;
            const bool is_generated_existing = path_exists(generated_root, is_generated_query_ok);
            if (!is_generated_query_ok)
            {
                result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, to_utf8(generated_root),
                                                    "能查询生成目录属性", "属性查询失败", to_utf8(generated_root),
                                                    "修正目录权限后重跑");
                return result;
            }
            if (!is_generated_existing)
            {
                const RemoveOutcome created = ID::Project::create_directories(generated_root);
                if (!created.is_ok)
                {
                    if (created.diagnostic.has_value())
                    {
                        result.diagnostic = created.diagnostic;
                    }
                    for (auto directory = m_created_directories.rbegin(); directory != m_created_directories.rend();
                         ++directory)
                    {
                        (void)remove_empty_directory(*directory);
                    }
                    m_created_directories.clear();
                    return result;
                }
                m_created_directories.push_back(generated_root);
                result.created_directories = true;
            }
        }

        const std::filesystem::path lock_path = record_path(project_root, kLockRelative);
        const ProjectLock::AcquireResult acquired = m_lock.acquire(lock_path, LockScope::Project);
        if (acquired.verdict != LockVerdict::Acquired)
        {
            result.diagnostic = acquired.diagnostic.has_value()
                                    ? acquired.diagnostic
                                    : std::optional<ProjectDiagnostic>(make_diagnostic(
                                          acquired.verdict == LockVerdict::WriterConflict
                                              ? ProjectCategory::WriterConflict
                                              : ProjectCategory::WriteFailed,
                                          to_utf8(lock_path), "取得项目锁", "取锁失败", to_utf8(lock_path),
                                          "等另一个写入者结束后重跑；不要手工删除活跃进程的锁文件"));
            for (auto directory = m_created_directories.rbegin(); directory != m_created_directories.rend(); ++directory)
            {
                (void)remove_empty_directory(*directory);
            }
            m_created_directories.clear();
            return result;
        }
        m_is_open = true;

        // 锁内一次读取两份记录：摘要与 base 完整内容同源
        std::optional<ProjectDiagnostic> diagnostic;
        const bool is_resolved_ok = read_record_snapshot(record_path(project_root, kResolvedRelative), kResolvedRelative,
                                                        m_base_resolved, diagnostic);
        if (!is_resolved_ok)
        {
            result.diagnostic = diagnostic;
            close();
            return result;
        }
        const bool is_manifest_ok = read_record_snapshot(record_path(project_root, kManifestRelative), kManifestRelative,
                                                        m_base_manifest, diagnostic);
        if (!is_manifest_ok)
        {
            result.diagnostic = diagnostic;
            close();
            return result;
        }

        m_base_digests = BaseDigests{};
        if (m_base_resolved.is_present)
        {
            Digest parsed;
            if (!digest_from_hex(m_base_resolved.digest, parsed))
            {
                result.diagnostic = make_diagnostic(ProjectCategory::RecordInvalid, std::string(kResolvedRelative),
                                                    "64 位十六进制摘要", m_base_resolved.digest,
                                                    std::string(kResolvedRelative), "重新生成该记录后重跑");
                close();
                return result;
            }
            m_base_digests.resolved_digest = parsed;
        }
        if (m_base_manifest.is_present)
        {
            Digest parsed;
            if (!digest_from_hex(m_base_manifest.digest, parsed))
            {
                result.diagnostic = make_diagnostic(ProjectCategory::RecordInvalid, std::string(kManifestRelative),
                                                    "64 位十六进制摘要", m_base_manifest.digest,
                                                    std::string(kManifestRelative), "重新生成该记录后重跑");
                close();
                return result;
            }
            m_base_digests.manifest_digest = parsed;
        }

        result.is_ok = true;
        return result;
    }

    const Session::BaseDigests& Session::base_digests() const noexcept
    {
        return m_base_digests;
    }

    const RecordSnapshot& Session::base_resolved() const noexcept
    {
        return m_base_resolved;
    }

    const RecordSnapshot& Session::base_manifest() const noexcept
    {
        return m_base_manifest;
    }

    void Session::close() noexcept
    {
        m_lock.release();
        m_is_open = false;
        m_created_directories.clear();
    }

    bool Session::is_open() const noexcept
    {
        return m_is_open;
    }

    const std::filesystem::path& Session::project_root() const noexcept
    {
        return m_project_root;
    }

    std::string make_attempt_id(std::string_view generation_id)
    {
        std::string attempt(generation_id.substr(0, 8));
        if (attempt.empty())
        {
            attempt = "00000000";
        }
        attempt += '-';
        attempt += to_decimal(current_pid());
        attempt += '-';
        attempt += to_decimal(++g_attempt_sequence);
        return attempt;
    }
} // namespace ID::Project
