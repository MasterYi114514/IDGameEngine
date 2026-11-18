#include "project/Apply/Journal.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "project/Base/FileIo.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordJson.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  `intent.json` 的逐项推进。
 *
 *  每次推进都做"改内存 → 全量序列化 → 原子替换"，因此磁盘上永远是完整合法 JSON；这比局部改写简单，
 *  也避免半行写入。写入失败不再尝试第二次：调用方按 `write_failed`（2）停止后续动作。
 */

namespace ID::Project
{
    Journal::BeginResult Journal::begin(const std::filesystem::path& intent_path, const IntentRecord& record)
    {
        m_intent_path  = intent_path;
        m_record       = record;
        m_is_started   = true;
        m_is_committed = false;
        return persist();
    }

    Journal::BeginResult Journal::advance(std::string_view operation_id, OperationStatus status,
                                          std::string_view actual)
    {
        BeginResult result;

        for (IntentOperation& operation : m_record.operations)
        {
            if (operation.id != operation_id)
            {
                continue;
            }
            operation.status = status;
            operation.actual = std::string(actual);
            return persist();
        }

        result.diagnostic =
            ProjectDiagnostic{ProjectCategory::WriteFailed, std::string(operation_id), "intent 里存在该操作项",
                              "未找到该操作项", std::string(kIntentRelative),
                              "这是实现缺陷：报告该操作项标识，不要手工编辑 intent.json"};
        return result;
    }

    Journal::BeginResult Journal::note_backup(std::string_view backup_relative)
    {
        m_record.backups.emplace_back(backup_relative);
        return persist();
    }

    Journal::BeginResult Journal::set_phase(std::string_view phase)
    {
        m_record.phase = std::string(phase);
        return persist();
    }

    Journal::BeginResult Journal::note_created_directory(std::string_view relative)
    {
        m_record.created_dirs.emplace_back(relative);
        return persist();
    }

    void Journal::mark_committed() noexcept
    {
        m_is_committed = true;
    }

    void Journal::mark_removed() noexcept
    {
        m_is_removed = true;
    }

    bool Journal::is_committed() const noexcept
    {
        return m_is_committed;
    }

    const IntentRecord& Journal::record() const noexcept
    {
        return m_record;
    }

    Journal::BeginResult Journal::persist()
    {
        BeginResult result;
        if (m_is_removed)
        {
            // intent.json 已删除：再写会把收尾过的事实反写成"仍在进行中"
            result.is_ok = true;
            return result;
        }
        if (!m_is_started)
        {
            result.diagnostic = ProjectDiagnostic{ProjectCategory::WriteFailed, std::string(kIntentRelative),
                                                  "journal 已开始", "尚未 begin", std::string(kIntentRelative),
                                                  "这是实现缺陷：先调用 begin 再推进操作项"};
            return result;
        }

        WriteRequest request;
        request.target                = m_intent_path;
        request.bytes                 = serialize_intent(m_record);
        request.is_skip_if_identical  = false;   // 逐项推进必须真实落盘，不能因内容相同而跳过
        request.subject               = std::string(kIntentRelative);

        const WriteOutcome outcome = write_file_atomic(request);
        result.is_ok = outcome.is_ok;
        if (!outcome.is_ok)
        {
            result.diagnostic = outcome.diagnostic;
        }
        return result;
    }
} // namespace ID::Project
