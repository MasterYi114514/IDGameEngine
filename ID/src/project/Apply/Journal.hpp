#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "project/Base/Diagnostic.hpp"
#include "project/Record/IntentRecord.hpp"

/**
 *  attempt 的 journal：包住 `IntentRecord` 的写盘与逐项状态推进。
 *
 *  不变量：
 *  1. `begin` 在任何登记、选择或资产写入之前把完整 attempt 落盘（临时文件 + 原子替换）。
 *  2. 每个操作完成后立即 `advance` 一次，磁盘上任何时刻都是**完整合法 JSON**；不允许积攒到末尾统一写
 *     （spec §6.4），否则崩溃会留下无人认领的新文件。
 *  3. `mark_committed` 只改内存标志：`intent` 已在提交点之前随 journal 落盘过，不需要额外写。
 *  4. 本类只写 `intent.json`；解析由 `Record/RecordJson` 的 `parse_intent` 完成，本类不解析。
 *  5. 写入失败一律 `write_failed`（2）：调用方不得在 `begin` 失败后继续任何写操作。
 */

namespace ID::Project
{
    /**
     *  journal：持有内存里的 attempt 与它的落盘路径。
     */
    class Journal
    {
    public:
        /**
         *  一次落盘的结果：失败时 `diagnostic` 是 `write_failed`。
         */
        struct BeginResult
        {
            bool                             is_ok      = false;
            std::optional<ProjectDiagnostic> diagnostic = {};
        };

        /**
         *  @brief 在任何登记、选择或资产写入之前写 `intent.json`
         *  @param intent_path `<项目根>/IDGenerated/intent.json`
         *  @param record 完整 attempt 内容（含 base 与 target 快照、逐项 before/after、payload 位置）
         *  @return 落盘结果；失败即 `write_failed`，调用方不得继续任何写操作
         */
        BeginResult begin(const std::filesystem::path& intent_path, const IntentRecord& record);

        /**
         *  @brief 推进一个操作项的状态与实测摘要，并立即落盘
         *  @param operation_id 操作项标识（U4 §4.9 的规则）
         *  @param status 新状态
         *  @param actual 实测摘要（十六进制，可为空）
         *  @return 落盘结果；操作项不存在属调用方违约，按 `write_failed` 报告且不改记录
         */
        BeginResult advance(std::string_view operation_id, OperationStatus status, std::string_view actual);

        /**
         *  @brief 记录一次备份路径（`--force` 备份成功后追加）
         */
        BeginResult note_backup(std::string_view backup_relative);

        /**
         *  @brief 记录当前阶段标识（`p2`/`p3`/`p4`/`p5`/`p6`）
         */
        BeginResult set_phase(std::string_view phase);

        /**
         *  @brief 记录一个本次创建的目录（`intent.created_dirs`）
         *  @note 在真正创建之前调用：崩溃后按该清单逆序只删空的目录
         */
        BeginResult note_created_directory(std::string_view relative);

        /**
         *  @brief 表示已到达提交点：此后 intent 的删除失败不再改变"已提交"事实
         */
        void mark_committed() noexcept;

        /**
         *  @brief 表示 `intent.json` 已被删除
         *  @note 之后的 `advance` / `note_*` 不再写盘：否则会把刚删掉的 `intent.json` 重新写出来
         */
        void mark_removed() noexcept;

        /**
         *  @brief 是否已到达提交点（`resolved.json` 已原子替换）
         */
        bool is_committed() const noexcept;

        /**
         *  @brief 内存里的 attempt 内容
         */
        const IntentRecord& record() const noexcept;

    private:
        BeginResult persist();

        std::filesystem::path m_intent_path  = {};
        IntentRecord          m_record       = {};
        bool                  m_is_started   = false;
        bool                  m_is_committed = false;
        bool                  m_is_removed   = false;
    };
} // namespace ID::Project
