#pragma once

#include <string>
#include <vector>

#include "project/Plan/PlanTypes.hpp"
#include "project/Plan/Preflight.hpp"
#include "project/Resolve/Resolution.hpp"

/**
 *  P1 计划：把解析结果、记录与磁盘状态合成完整 plan（动作序列 + 两类冲突集合 + 记录预期内容）。
 *
 *  本单元只读磁盘（D）与记录（O），所有写入都变成 `PlanAction`；`--check` 与真实执行共用本函数，
 *  `is_check` 只影响调用方是否把计划交给执行阶段，不改变动作集合（D2）。
 *
 *  不变量：
 *  1. 生成顺序固定：保留集 → 写入集合 → 根文件两个受控片段 → 裁剪 → seed → 记录 → 计划摘要 → 动作序列。
 *  2. `generation_id` 不含计划摘要，也不依赖任何记录字节；`manifest_digest` 依赖 manifest 的最终文本，
 *     因此必须先定身份、再构造记录，计划摘要最后算。
 *  3. 片段是两个独立条目：各自算 D/O/N，摘要只经 `ID::CMake::fragment_range_digest` 对最终整文件字节计算，
 *     不自行拼接片段文本再哈希，也不用整文件摘要顶替。
 *  4. 计划里的动作顺序即执行顺序；`payload` 相对路径含 attempt 标识，由执行阶段填（计划留空）。
 *  5. `--check` 的三类清单与差异码由本单元的 `format_check_report` / `has_plan_diff` 给出；计划差异不是
 *     诊断类别（`Error.hpp` 无该取值）。
 */

namespace ID::Project
{
    /**
     *  计划选项。`has_root_cmake` 与 `marker_matches` 由 P0 读到的磁盘状态给出。
     */
    struct PlanOptions
    {
        CommandKind command        = CommandKind::Update;
        bool        has_force      = false;
        bool        is_check       = false;
        bool        has_root_cmake = false;   // 根 CMakeLists.txt 是否已存在
        bool        marker_matches = true;    // is_initialized 是否已与记录一致
    };

    /**
     *  @brief P1：把解析结果、记录与磁盘状态合成完整 plan
     *  @param assembly 已解析结果（生成文本摘要须已回填，见 U8）
     *  @param preflight P0 结果
     *  @param options 计划选项
     *  @return 完整计划；存在冲突或内部不一致时 `is_valid` 为假且 `diagnostics` 非空
     *  @note 保留集来源按命令分开：`new`/`update` 用 `build_retention`，`clean` 用 `retention_from_record`
     *  @note 生成文本由本函数调用 `emit_all` 取得：受控片段的正文字节与 `limitations` 只在那里产出，
     *        调用方须已用 `apply_generated_digests` 回填整文件生成项的摘要
     *  @note 续做窗口：`preflight.records.intent` 的命令与本次命令相同（`recover` 不比较）时，
     *        `intent.operations` 里已写出的 `(path, fragment)` 以该操作的 before/after 判三态——
     *        当前等于 after 视为已落盘、等于 before 按正常写入重做、第三态报 `recovery_conflict`（2）。
     *        该证据只对本 attempt 自己的写入项成立，不放宽"`O = null` 且无证据"的未登记判定
     *  @note 写入与裁剪目标的祖先链上任一段命中重解析点即按 `path_outside_project`（1）拒绝，
     *        判在归属与摘要判定之前：越界不是内容冲突，`--force` 不放行（spec §2.3）
     *  @note `next_manifest` 只保存存活条目；删除事实记在 `intent` 的删除操作里
     *  @note `resolved.manifest_digest` 用**刚写出的 manifest 字节**计算：`WriteManifest` 动作携带的就是
     *        同一份字节，执行与恢复都不重新序列化
     */
    ProjectPlan build_plan(const ResolvedAssembly& assembly, const PreflightResult& preflight,
                           const PlanOptions& options);

    /**
     *  @brief no-op 判定：目标内容集合与记录摘要都已达
     *  @param plan 计划
     *  @return 计划无需 intent、无需写记录、无需动内容文件时返回 true
     *  @note 判定只用两件事：目标内容集合（每个写入项的当前 D 等于其 N，片段项按片段文本范围比较）与
     *        记录摘要（`next_manifest`/`next_resolved` 的序列化字节摘要等于既有两份记录摘要）
     *  @note 不按"删除集合是否为空"判定：删除集合非空必然使记录摘要变化，由记录摘要那条覆盖
     *  @note `AddressScope::Engine` 的 `UpdateMarker` 不参与判定：标记是派生缓存
     *  @note no-op 成立时计划里不再保留 `WriteManifest` / `WriteResolved` / `DeleteIntent`：两份记录的字节
     *        已与目标一致，重写会改变文件时间戳并违背"内容无变化的 update 不写 intent、不改两份记录"
     *        （spec §8.9）；只修派生缓存的那条路径因此仍可执行 `UpdateMarker`
     */
    bool is_noop_plan(const ProjectPlan& plan) noexcept;

    /**
     *  @brief `--check` 的差异判定
     *  @param plan 计划
     *  @return 有冲突时 false（冲突自己给码）；只有写入、删除或登记重写时 true；三类皆空时 false
     *  @note 调用方按 spec §13.1 取码：冲突优先，其次差异 6，最后 0
     */
    bool has_plan_diff(const ProjectPlan& plan) noexcept;

    /**
     *  @brief `--check` 的三类清单文本（写入 → 删除 → 冲突，类内按 `(path, fragment)` 字节序）
     *  @param plan 计划
     *  @return 每行一条的人读文本；空计划返回空容器
     *  @note 输出顺序即 spec §13.1 的稳定排序；差异码与冲突码不在这里换算，由 `select_exit_code` 决定
     */
    std::vector<std::string> format_check_report(const ProjectPlan& plan);
} // namespace ID::Project
