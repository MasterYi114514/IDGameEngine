#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Plan/PlanTypes.hpp"

/**
 *  只读快照的取样与复验：`list` 与所有写命令的 `--check` 共用同一份比较实现
 *  （spec §16 第 17 条、§13.2 `snapshot_unstable`、docs/id/10 §10.12）。
 *
 *  `--check` 与 `list` 不取锁，因此它们靠"读取前后各取一次目标文件摘要"证明本次判定看到的状态在输出
 *  结论之前没有被外部改写；摘要变化即报 `snapshot_unstable`（2），并放弃基于过期快照的清单输出。
 *
 *  受检目标是**该命令的状态文件**，不是它读到的全部文件：登记文件 `ID/project.json`、选择
 *  `plugins.json`、两份记录、`IDGenerated/intent.json` 与根 `CMakeLists.txt`；`build` 另含
 *  `IDGenerated/build.json`（`run_build` 复验构建记录时读它）。其余读取不纳入，理由分三类：
 *  1. 受管树（项目内插件副本与生成区内容）逐文件读摘要，只读窗口随插件数与文件数线性放大；它们与
 *     记录的一致性由 `stale_generated` 与 `manifest` 的逐条校验判定，不靠时间窗口。
 *  2. `IDGenerated/before/*`（before-payload）是 attempt 私有数据：只有 `recover --action restore`
 *     读它，且逐项在读取时按 `operation.before` 摘要校验，不需要时间窗口。
 *  3. `CMakeCache.txt` 由构建自身写：并发的别的构建会改它，把它算作不稳定会把"别处正在构建"误报成
 *     本命令结论失效。
 *
 *  不变量：
 *  1. 取样只读：不建目录、不取锁、不写临时文件，也不改变文件时间戳语义。
 *  2. 摘要以十六进制文本表达，空串表示目标不存在；"存在 → 不存在"与"不存在 → 存在"都算摘要变化。
 *  3. 一次比较的 `targets` 与 `before` 顺序一致、长度相同，比较不重排、不筛除、不跳过。
 *  4. 比较只有一份实现：`list` 与 `--check` 都调 `compare_snapshot`，不各写一套。
 *  5. 目标分两段取样：登记文件在项目根确定之前就要读，项目内文件在其后；两段追加进同一对序列，
 *     顺序仍是登记在前，比较端只遍历一次。
 */

namespace ID::Project
{
    /**
     *  一个受检目标：绝对路径与它在诊断里的两种说法。
     *  纯数据载体，由调用方一次填好；取样与比较都不修改它。
     */
    struct SnapshotTarget
    {
        std::filesystem::path   path        = {};   // 目标绝对路径
        std::string             subject     = {};   // 诊断对象与定位：登记文件用绝对路径，项目内文件用相对路径
        std::string             description = {};   // 诊断措辞里的文件说法，自带与相邻中文的分隔空格
    };

    /**
     *  @brief 一次读取结果的摘要文本
     *  @param read 读取结果
     *  @return 读到内容时返回其 SHA-256 十六进制文本；不存在或读取失败返回空串
     *  @note 读取失败与不存在同样按空串表达：两种情况都让"读到的内容"无法证明，
     *        与"存在性发生变化"一样落在快照变化的判定里
     */
    std::string snapshot_digest(const FileRead& read);

    /**
     *  @brief 按目标顺序取样一次
     *  @param targets 受检目标
     *  @return 与 `targets` 一一对应的摘要文本；空串表示该目标不存在
     *  @note 只读；目标都是状态文件，按整文件读取后取摘要，与其它读取路径同一口径
     */
    std::vector<std::string> sample_snapshot(std::span<const SnapshotTarget> targets);

    /**
     *  @brief 追加一段取样：把 `extra` 的目标与摘要接到同一对序列尾部
     *  @param targets 已有目标序列，就地追加
     *  @param before 已有摘要序列，就地追加；追加后仍与 `targets` 顺序一致、长度相同
     *  @param extra 本次要追加的目标
     *  @note 分段取样用：登记文件在项目根确定之前取样，项目内文件在其后；两段共用一对序列，
     *        比较端因此仍只有一次遍历
     */
    void append_snapshot(std::vector<SnapshotTarget>& targets, std::vector<std::string>& before,
                         std::span<const SnapshotTarget> extra);

    /**
     *  @brief 复验：逐目标重读并与 `before` 比较，任一摘要不同即追加 `snapshot_unstable`（2）
     *  @param diagnostics 诊断集合，就地追加；同一目标只追加一条
     *  @param targets 受检目标（与 `before` 顺序一致）
     *  @param before `sample_snapshot` 的取样结果
     *  @param command_text 诊断"下一步"里可复制的重跑命令，如 `ID project update --check`
     *  @return 全部目标摘要未变时返回 true；任一变化返回 false
     *  @note 类别与码的换算不在这里：追加的是 `snapshot_unstable`，退出码统一由 `select_exit_code` 给出
     *  @note 两个序列由同一条取样路径产生、长度必然一致；遍历仍按较短者进行，不越界访问
     */
    bool compare_snapshot(DiagnosticSet& diagnostics, std::span<const SnapshotTarget> targets,
                          std::span<const std::string> before, std::string_view command_text);

    /**
     *  @brief 登记文件的受检目标
     *  @param registry_path 登记文件绝对路径（`<工程根>/ID/project.json`）
     *  @return 单元素目标：对象与定位都用绝对路径，措辞用" 登记文件 "
     *  @note 登记文件在项目根之外，且必须先于 `load_registry` 取样：`project_exists`、
     *        `project_not_found` 与 `is_initialized` 的差异都由它决定，取样晚于读取就证明不了读取时的状态
     */
    SnapshotTarget registry_snapshot_target(const std::filesystem::path& registry_path);

    /**
     *  @brief 一条命令在 `--check` 下要复验的项目内状态文件（登记文件由 `registry_snapshot_target` 给）
     *  @param project_root 项目根绝对路径
     *  @param command 命令种类
     *  @return 固定顺序的目标列表：`plugins.json`（`clean` 不读它）→ `resolved.json` → `manifest.json`
     *          → `intent.json` → 根 `CMakeLists.txt`（仅生成计划的 `new`/`update`/`clean`）
     *          → `build.json`（仅 `build`）
     *  @note `intent.json` 对每条命令都成立：P0 对五条命令都读它判定未完成事务
     *  @note 受管树、before-payload 与 `CMakeCache.txt` 不在列表内，理由见文件头第 1–3 条
     */
    std::vector<SnapshotTarget> check_snapshot_targets(const std::filesystem::path& project_root,
                                                       CommandKind command);
} // namespace ID::Project
