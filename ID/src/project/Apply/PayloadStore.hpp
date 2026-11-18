#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Record/IntentRecord.hpp"

/**
 *  before-payload：任何受管文件或受控片段在替换、删除之前的旧字节（spec §1.4）。
 *
 *  它与 `--force` 备份是两套数据，互不代替：payload 是事务恢复素材（本 attempt 私有，随收尾删除），
 *  备份是"用户放弃本地修改"的取证（只增不删）。
 *
 *  不变量：
 *  1. 写 payload 后必须回读校验摘要等于该项记录的 `before`；不符按 `write_failed`（2），且调用方不得继续
 *     该文件的变更——不出现"已替换但无 before 素材"的中间态。
 *  2. `expected_before.is_absent` 为真时不写 payload，但必须确认源确实不存在；源存在即 `changed_since_plan`（2）。
 *  3. 片段 payload 与普通文件 payload 分两个命名空间（`<before>/<attempt>/fragments/<fragment>.txt` 与
 *     `<before>/<attempt>/<相对路径>`），同一根文件的两个片段是两个独立文件。
 *  4. `discard_attempt` 是 payload 收尾的唯一入口：只按 attempt 名删那一个目录，不递归删 `before/` 根、
 *     不按通配符批量删（`references/memory.md` §7）。
 */

namespace ID::Project
{
    /**
     *  before-payload 的写入与回读。
     */
    class PayloadStore
    {
    public:
        /**
         *  一次写入的结果：`is_written` 为假表示原对象不存在（`before = absent`）。
         */
        struct Result
        {
            bool                             is_ok      = false;
            bool                             is_written = false;
            Digest                           digest     = {};
            std::optional<ProjectDiagnostic> diagnostic = {};
        };

        /**
         *  @brief 绑定项目根
         *  @param project_root 项目根绝对路径
         */
        explicit PayloadStore(const std::filesystem::path& project_root);

        /**
         *  @brief 保存普通文件的旧字节
         *  @param payload_relative `IDGenerated/before/<attempt>/<相对路径>`
         *  @param source_absolute 被替换文件的绝对路径；不存在时直接返回 `is_written = false`
         *  @param expected_before 该项记录的 before 摘要；写入前后都据此校验
         *  @return 写入结果；源内容与 `expected_before` 不符按 `changed_since_plan`，
         *          回读不符按 `write_failed`
         */
        Result save_file(std::string_view payload_relative, const std::filesystem::path& source_absolute,
                         const DigestState& expected_before);

        /**
         *  @brief 保存受控片段的旧文本范围
         *  @param payload_relative `IDGenerated/before/<attempt>/fragments/<fragment>.txt`
         *  @param fragment_bytes 片段文本范围（含 BEGIN/END 标记行）
         *  @param expected_before 该项记录的 before 摘要
         *  @return 写入结果；语义同 `save_file`
         */
        Result save_fragment(std::string_view payload_relative, std::string_view fragment_bytes,
                             const DigestState& expected_before);

        /**
         *  一次回读的结果：`is_present` 为假表示 payload 文件不存在。
         */
        struct PayloadRead
        {
            bool        is_ok      = false;
            bool        is_present = false;
            std::string bytes      = {};
            Digest      digest     = {};
        };

        /**
         *  @brief 读回 payload 用于 restore
         *  @param payload_relative 相对项目根的 payload 路径
         *  @return 读取结果；`is_ok` 为假表示读取失败，`is_present` 为假表示文件不存在
         *  @note 摘要必须由调用方与该项 `before` 比较：缺失或损坏按 `recovery_conflict`（2）
         */
        PayloadRead load(std::string_view payload_relative);

        /**
         *  @brief 收尾一个 attempt 的 before-payload：删除 `IDGenerated/before/<attempt>/`
         *  @param attempt attempt 标识（`intent.attempt`）
         *  @return 删除结果；目录不存在视为成功；删除失败按 `write_failed`（2）报告，残留留给下一次收尾
         *  @note 只按 attempt 名定位那一个目录；目录内的文件逐条删除、目录自底向上删除，不递归删 `before/` 根
         */
        RemoveOutcome discard_attempt(std::string_view attempt);

    private:
        Result save_bytes(std::string_view payload_relative, std::string_view bytes,
                          const DigestState& expected_before);

        std::filesystem::path m_project_root = {};
    };
} // namespace ID::Project
