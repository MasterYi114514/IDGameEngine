#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/PathText.hpp"
#include "project/Record/IntentRecord.hpp"
#include "project/Record/ManifestRecord.hpp"
#include "project/Record/ResolvedRecord.hpp"

/**
 *  计划的数据载体：命令种类、动作种类、动作项、冲突项与完整计划。
 *
 *  本头只持数据与两个纯查询（取记录写入的字节、动作是否属于内容项），不做判定：
 *  谁该出现哪种动作由 Planner 决定，动作如何落盘由 Apply 决定。
 *
 *  不变量：
 *  1. 动作容器顺序即执行顺序；执行方不得重排、不得新增未在计划里的动作（spec §8.4）。
 *  2. `address_scope` 与 `kind` 一一对应：`RegisterProject` 与 `UpdateMarker` 是
 *     `AddressScope::Engine`（唯一目标是 `<工程根>/ID/project.json`），其余动作是
 *     `AddressScope::Project`（经 `normalize_relative` 的相对项目根路径）。
 *  3. 记录写入的字节只有一个来源：`WriteManifest` / `WriteResolved` 动作的 `bytes`。
 *     `resolved.manifest_digest` 就是这份 manifest 字节的摘要，`intent` 的 target 快照也取同一份字节。
 *  4. `plan_digest` 只服务 `intent` 与 `--check` 差异说明，不是 `resolved` 字段，也不进入 `generation_id`。
 *  5. `selection_digest` 是 `intent` 的输入摘要位：计划一次算出，执行与恢复都不重算规范化有效选择。
 */

namespace ID::Project
{
    /**
     *  命令种类。取值与 spec §9.1 的命令表一一对应；`List` 只读，不产生计划。
     */
    enum class CommandKind : std::uint8_t
    {
        New     = 0,
        Update  = 1,
        Build   = 2,
        Clean   = 3,
        Recover = 4,
        List    = 5,
    };

    /**
     *  动作种类。`WriteManifest` 之后的五个是控制面动作（记录、登记、派生缓存），
     *  前七个是内容面动作。
     */
    enum class PlanActionKind : std::uint8_t
    {
        CreateDirectory = 0,
        WriteFile       = 1,
        WriteFragment   = 2,
        DeleteFile      = 3,
        BackupFile      = 4,
        BackupFragment  = 5,
        PayloadFile     = 6,
        PayloadFragment = 7,
        WriteManifest   = 8,    // 记录写入：manifest 先
        WriteResolved   = 9,    // 提交点
        DeleteIntent    = 10,
        UpdateMarker    = 11,   // is_initialized 派生缓存（Engine 作用域）
        RegisterProject = 12,   // 仅 new：写 project.json 的新登记（is_initialized = false；Engine 作用域）
        WriteSelection  = 13,   // 仅 new：首次写出 plugins.json（Project 作用域）
    };

    /**
     *  一个动作项。数据载体：由 Planner 一次填好，Apply 只读并按 `kind` 落盘。
     */
    struct PlanAction
    {
        std::string                id             = {};                             // 与 intent 操作项同一标识规则
        PlanActionKind             kind           = PlanActionKind::WriteFile;
        AddressScope               address_scope  = AddressScope::Project;
        std::string                path           = {};                             // Project：相对项目根；Engine：ID/project.json
        std::optional<std::string> fragment       = {};
        PayloadType                payload_type   = PayloadType::File;
        DigestState                before         = {};
        DigestState                after          = {};
        std::string                payload        = {};                             // 相对项目根的 payload 路径，可空
        std::string                backup         = {};                             // 相对项目根的备份路径，可空
        std::string                bytes          = {};                             // 写入内容（文件/片段体/记录文本）
        std::string                source         = {};                             // 复制来源绝对路径
        bool                       is_copy        = false;                          // 真表示内容是流式复制，bytes 为空
    };

    /**
     *  一条冲突。冲突只在两类集合里表达（写入冲突 `unmanaged_content`、裁剪冲突
     *  `cleanup_blocked`），不新开类别。
     */
    struct PlanConflict
    {
        std::string                path     = {};
        std::optional<std::string> fragment = {};
        std::string                category = {};   // "unmanaged_content" / "cleanup_blocked"
        std::string                detail   = {};   // 摘要关系的人读描述
    };

    /**
     *  一次计划：动作序列、两类冲突集合、记录预期内容与诊断。
     */
    struct ProjectPlan
    {
        CommandKind                  command             = CommandKind::Update;
        std::string                  generation_id       = {};   // 取自 assembly，不由计划摘要派生
        std::string                  selection_digest    = {};   // 规范化有效选择摘要（intent 的输入摘要位）
        Digest                       plan_digest         = {};   // 只进 intent 与 --check 差异说明
        ManifestRecord               next_manifest       = {};
        ResolvedRecord               next_resolved       = {};
        std::vector<PlanAction>      actions             = {};   // 执行顺序即容器顺序
        std::vector<PlanConflict>    write_conflicts     = {};
        std::vector<PlanConflict>    cleanup_conflicts   = {};
        std::vector<std::string>     mkdirs              = {};
        std::size_t                  content_write_count = 0;    // 供 --check 输出统计
        std::size_t                  delete_count        = 0;
        bool                         is_noop             = false;  // 无需 intent、无需写记录
        DiagnosticSet                diagnostics         = {};
        bool                         is_valid            = false;
    };

    /**
     *  @brief 取记录写入动作携带的字节
     *  @param plan 计划
     *  @param kind 动作种类（`WriteManifest` 或 `WriteResolved`）
     *  @return 命中时返回该动作字节的只读指针；计划里没有该动作时返回 nullptr
     *  @note 记录字节的唯一来源：执行与恢复都经本函数取同一份字节，不重新序列化
     */
    inline const std::string* find_record_bytes(const ProjectPlan& plan, PlanActionKind kind) noexcept
    {
        for (const PlanAction& action : plan.actions)
        {
            if (action.kind == kind)
            {
                return &action.bytes;
            }
        }
        return nullptr;
    }

    /**
     *  @brief 动作是否是控制面动作（记录、登记、派生缓存）
     *  @param kind 动作种类
     *  @return `RegisterProject` / `WriteManifest` / `WriteResolved` / `DeleteIntent` / `UpdateMarker` 返回 true
     *  @note 控制面动作不进入 `intent.operations` 的内容项集合（U5 §5.8），也不计入写入文件数
     */
    inline bool is_control_action(PlanActionKind kind) noexcept
    {
        switch (kind)
        {
            case PlanActionKind::WriteManifest:
            case PlanActionKind::WriteResolved:
            case PlanActionKind::DeleteIntent:
            case PlanActionKind::UpdateMarker:
            case PlanActionKind::RegisterProject:
                return true;
            default:
                return false;
        }
    }

    /**
     *  @brief 动作是否属于内容项（`file` 或 `fragment` 粒度的写入与删除）
     *  @param kind 动作种类
     *  @return `WriteFile` / `WriteFragment` / `DeleteFile` 返回 true
     *  @note 这是 `intent` 的 `OperationKind::Write`/`Delete` 来源，也是 `continue` 判定的对象集合
     */
    inline bool is_content_action(PlanActionKind kind) noexcept
    {
        return kind == PlanActionKind::WriteFile || kind == PlanActionKind::WriteFragment
            || kind == PlanActionKind::DeleteFile;
    }
} // namespace ID::Project
