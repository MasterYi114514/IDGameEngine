#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "project/Base/Digest.hpp"
#include "project/Base/PathText.hpp"

/**
 *  `intent.json`：进行中的 attempt（spec §6.4）。
 *
 *  本头是载体：写入时机、逐项状态推进与恢复判定属于执行与恢复阶段。载荷与备份路径由 RecordPaths 生成，
 *  本头不手写拼接。
 *
 *  不变量：
 *  1. `payload_type` 与 `fragment` 必须一致：`File` 时 `fragment` 为空，`Fragment` 时必须非空。
 *  2. `address_scope` 与 `kind` 必须一致：`Engine` 只允许 `RegisterWrite` 且 `path` 只能是
 *     `ID/project.json`（相对工程根）、`fragment` 为空；`Project` 不允许 `RegisterWrite`。
 *  3. 摘要状态有三种取值：`absent` 或某个具体摘要。用 `is_absent` 区分，不把 absent 编码成空串
 *     （空串与"未计算"不可区分）。
 *  4. 同一 `intent.json` 内操作项 `id` 唯一；重号即 `record_invalid`。
 *  5. `next_argv` 与本次实际参数一致（含或不含选择参数），用于诊断里给出可复制的续做命令。
 */

namespace ID::Project
{
    /**
     *  attempt 的命令来源。
     */
    enum class IntentCommand : std::uint8_t
    {
        New     = 0,
        Update  = 1,
        Clean   = 2,
        Recover = 3,
    };

    /**
     *  摘要对象的粒度。
     */
    enum class PayloadType : std::uint8_t
    {
        File     = 0,   // fragment 为空，摘要对象是整文件字节
        Fragment = 1,   // path 为所属文件，fragment 必填，摘要对象是片段文本范围
    };

    /**
     *  操作项种类。
     */
    enum class OperationKind : std::uint8_t
    {
        Mkdir          = 0,   // 创建目录
        Write          = 1,   // 写项目内文件或受控片段
        Delete         = 2,   // 删项目内文件或片段
        Backup         = 3,   // --force 备份
        Payload        = 4,   // before-payload
        RegisterWrite  = 5,   // 工程根登记写入（ID/project.json），只与 AddressScope::Engine 配对
    };

    /**
     *  操作项完成状态。
     */
    enum class OperationStatus : std::uint8_t
    {
        Pending = 0,
        Done    = 1,
        Failed  = 2,
    };

    /**
     *  摘要状态：absent 或某个具体摘要。
     */
    struct DigestState
    {
        bool        is_absent = false;   // 真表示该侧不存在（新建项 before、删除项 after）
        std::string hex       = {};      // is_absent 为假时有效，64 位小写十六进制
    };

    /**
     *  一个操作项。
     */
    struct IntentOperation
    {
        std::string      id             = {};                              // 稳定标识，见 docs/id/04 §4.9
        OperationKind    kind           = OperationKind::Write;
        AddressScope     address_scope  = AddressScope::Project;           // 写入目标的路径命名空间
        std::string      path           = {};                              // 相对项目根；Engine 时是 ID/project.json
        std::optional<std::string> fragment = {};
        PayloadType      payload_type   = PayloadType::File;
        DigestState      before         = {};
        DigestState      after          = {};
        std::string      payload        = {};                              // 相对项目根的 payload 路径，可空
        std::string      backup         = {};                              // 相对项目根的备份路径，可空
        OperationStatus  status         = OperationStatus::Pending;
        std::string      actual         = {};                              // 完成后的实测摘要，可为空
    };

    /**
     *  一份记录的完整快照（base 或 target）。
     */
    struct RecordSnapshot
    {
        bool        is_present = false;   // 快照是否存在；首次 new 的 base 为 false
        std::string bytes      = {};      // 完整 JSON 文本
        std::string digest     = {};      // bytes 的摘要
    };

    /**
     *  `intent.json` 的完整内容（键序与写出顺序见 IntentJson.cpp）。
     */
    struct IntentRecord
    {
        std::string   attempt        = {};   // attempt 标识：生成身份 + 序号，进程内唯一
        std::string   schema_version = {};
        std::string   rules_version  = {};
        IntentCommand command        = IntentCommand::Update;

        std::string   generation_id     = {};   // 目标代
        std::string   selection_digest  = {};   // 输入摘要（规范化有效选择）
        Digest        plan_digest       = {};   // 规范化计划摘要；不进 generation_id、不写成 resolved 字段

        RecordSnapshot base_resolved        = {};   // 首次 new 时 is_present = false
        RecordSnapshot base_manifest        = {};
        bool           has_base_commit      = false;   // 是否有可回退的已提交代

        RecordSnapshot target_resolved      = {};   // 本次将写出的内容
        RecordSnapshot target_manifest      = {};

        std::vector<IntentOperation> operations    = {};   // 按执行顺序
        std::vector<std::string>     created_dirs  = {};   // 本次创建的目录（相对路径，按创建顺序）
        std::vector<std::string>     removed_dirs  = {};   // 本次删除的目录
        std::vector<std::string>     backups       = {};   // 已写入的备份相对路径
        std::string                  phase         = {};   // "p2" / "p3" / "p4" / "p5" / "p6"
        std::vector<std::string>     next_argv     = {};   // 收敛用的命令行（不含程序名）
    };
} // namespace ID::Project
