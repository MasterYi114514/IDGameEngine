#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "project/Base/Diagnostic.hpp"

/**
 *  `--force` 覆盖或删除被本地修改的受管文件前的副本（spec §1.4）。
 *
 *  它服务"用户放弃本地修改"的取证，与 before-payload 是两套数据：备份只增不删、`clean` 不删 `backup/`；
 *  payload 随 attempt 收尾删除。
 *
 *  不变量：
 *  1. 备份路径 `IDGenerated/backup/<generation>/<相对路径>` 与 `.../fragments/<fragment>.txt` 由 `RecordPaths`
 *     的构造入口给出，本单元不手写拼接。
 *  2. 复制或写入后必须回读复核摘要一致才返回成功；失败即中止该项（`write_failed`，2），不覆盖原文件。
 *  3. 只写备份目录，不动原文件；原文件的覆盖或删除由执行阶段在其后进行。
 */

namespace ID::Project
{
    /**
     *  `--force` 备份。
     */
    class BackupStore
    {
    public:
        /**
         *  一次备份的结果：`backup_relative` 是写入位置（相对项目根），失败时为空。
         */
        struct Result
        {
            bool                             is_ok           = false;
            std::string                      backup_relative = {};
            std::optional<ProjectDiagnostic> diagnostic      = {};
        };

        /**
         *  @brief 绑定项目根与目标 generation
         *  @param project_root 项目根绝对路径
         *  @param generation_id 目标代标识（备份目录按代分组）
         */
        BackupStore(const std::filesystem::path& project_root, std::string_view generation_id);

        /**
         *  @brief 备份一个整文件
         *  @param relative 目标相对路径（相对项目根），只用于构造备份路径与诊断
         *  @param source_absolute 被覆盖或删除文件的绝对路径
         *  @return 备份结果；源文件不可读或回读摘要不符按 `write_failed`
         *  @note 流式复制 + 回读校验（`references/memory.md` §9），不整文件读入内存
         */
        Result save_file(std::string_view relative, const std::filesystem::path& source_absolute);

        /**
         *  @brief 备份一个受控片段的文本范围
         *  @param relative 所属文件相对路径
         *  @param fragment_name 片段名
         *  @param fragment_bytes 片段文本范围（含 BEGIN/END 标记行）
         */
        Result save_fragment(std::string_view relative, std::string_view fragment_name,
                             std::string_view fragment_bytes);

    private:
        Result write_backup(std::string_view backup_relative, std::string_view bytes);

        std::filesystem::path m_project_root = {};
        std::string           m_generation   = {};
    };
} // namespace ID::Project
