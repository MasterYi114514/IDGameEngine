#include "project/Apply/BackupStore.hpp"

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
 *  `--force` 备份的落盘与复核。
 *
 *  整文件用流式复制（`copy_file_atomic` 自带回读校验），片段按文本范围原子写并回读比对摘要；
 *  两者都成功后才允许执行阶段覆盖或删除原对象。
 */

namespace ID::Project
{
    namespace
    {
        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        bool is_backup_path(std::string_view relative)
        {
            return normalize_relative(relative).verdict == PathVerdict::Ok && is_under(relative, kBackupDirectory);
        }
    } // namespace

    BackupStore::BackupStore(const std::filesystem::path& project_root, std::string_view generation_id)
        : m_project_root(project_root)
        , m_generation(generation_id)
    {
    }

    BackupStore::Result BackupStore::save_file(std::string_view relative,
                                               const std::filesystem::path& source_absolute)
    {
        Result result;

        const std::string backup = backup_relative(m_generation, relative, std::nullopt);
        if (!is_backup_path(backup))
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(relative),
                                                "备份路径落在 IDGenerated/backup/** 之内", backup,
                                                std::string(relative),
                                                "这是实现缺陷：备份路径必须由 RecordPaths 的构造入口生成");
            return result;
        }

        const std::filesystem::path target = m_project_root / to_native(backup);
        const std::string parent           = std::string(parent_relative(backup));
        if (!parent.empty())
        {
            const RemoveOutcome created = ID::Project::create_directories(m_project_root / to_native(parent));
            if (!created.is_ok)
            {
                result.diagnostic = created.diagnostic;
                return result;
            }
        }

        WriteRequest request;
        request.target               = target;
        request.is_skip_if_identical = false;
        request.subject              = std::string(relative);

        const CopyOutcome outcome = copy_file_atomic(source_absolute, request);
        if (!outcome.is_ok)
        {
            result.diagnostic = outcome.diagnostic;
            return result;
        }

        result.is_ok           = true;
        result.backup_relative = backup;
        return result;
    }

    BackupStore::Result BackupStore::save_fragment(std::string_view relative, std::string_view fragment_name,
                                                  std::string_view fragment_bytes)
    {
        const std::string backup = backup_relative(m_generation, relative, std::string_view(fragment_name));
        return write_backup(backup, fragment_bytes);
    }

    BackupStore::Result BackupStore::write_backup(std::string_view backup_relative_text, std::string_view bytes)
    {
        Result result;

        if (!is_backup_path(backup_relative_text))
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(backup_relative_text),
                                                "备份路径落在 IDGenerated/backup/** 之内",
                                                std::string(backup_relative_text),
                                                std::string(backup_relative_text),
                                                "这是实现缺陷：备份路径必须由 RecordPaths 的构造入口生成");
            return result;
        }

        const std::filesystem::path target = m_project_root / to_native(backup_relative_text);
        const std::string parent           = std::string(parent_relative(backup_relative_text));
        if (!parent.empty())
        {
            const RemoveOutcome created = ID::Project::create_directories(m_project_root / to_native(parent));
            if (!created.is_ok)
            {
                result.diagnostic = created.diagnostic;
                return result;
            }
        }

        const Digest expected = digest_text(bytes);

        WriteRequest request;
        request.target               = target;
        request.bytes                = std::string(bytes);
        request.is_skip_if_identical = false;
        request.subject              = std::string(backup_relative_text);

        const WriteOutcome outcome = write_file_atomic(request);
        if (!outcome.is_ok)
        {
            result.diagnostic = outcome.diagnostic;
            return result;
        }

        const FileRead read_back = read_file(target);
        if (read_back.status != ReadStatus::Found)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(backup_relative_text),
                                                "备份写后能回读", "回读失败", std::string(backup_relative_text),
                                                "确认备份目录可写后重跑原命令");
            return result;
        }
        if (digest_text(read_back.bytes).to_hex() != expected.to_hex())
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(backup_relative_text),
                                                expected.to_hex(), digest_text(read_back.bytes).to_hex(),
                                                std::string(backup_relative_text),
                                                "确认磁盘与杀毒软件未干扰写入后重跑原命令");
            return result;
        }

        result.is_ok           = true;
        result.backup_relative = std::string(backup_relative_text);
        return result;
    }
} // namespace ID::Project
