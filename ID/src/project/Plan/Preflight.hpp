#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Input/PluginCatalog.hpp"
#include "project/Input/Registry.hpp"
#include "project/Input/Selection.hpp"
#include "project/Plan/PlanTypes.hpp"
#include "project/Plan/Snapshot.hpp"
#include "project/Record/IntentRecord.hpp"
#include "project/Record/ManifestRecord.hpp"
#include "project/Record/ResolvedRecord.hpp"

/**
 *  P0 预检：参数、登记、路径身份、输入可用性、记录可信性与未完成事务的判定汇总。
 *
 *  本单元**只判定、不写盘**：不创建目录、锁文件、日志或临时文件（spec §0.5 第 2 条）；`--check` 与真实
 *  执行走同一套判定，差别只在于调用方拿到结论后是否继续。
 *
 *  不变量：
 *  1. `intent` 的收敛判定按 U5 §5.8 的顺序：先判"已提交窗口"，再判命令接管、base 可证、`selection_digest`
 *     与逐项内容是否已全部落盘；`base` 可证指"当前 == base 或 当前 == 本次目标"（提交未完成窗口），
 *     有 `intent` 时不因此落到 `record_invalid`。
 *  2. `clean` 不读 `plugins.json`、不读清单（spec §9.3 链路 4）：本函数对 `clean` 跳过选择与清单读取。
 *     `build`/`recover` 不读源插件库：选择复核与输入摘要只用 `resolved` 的校验快照（spec §8.3、§12.1），
 *     源插件目录整体不可用时这两个命令仍能通过 P0。
 *  3. `entry` 指向 `registry.entries` 内部元素：`PreflightResult` 被移动后指针失效，调用方在使用前重新取
 *     （或直接用 `project_root` 与 `entry->name`）。
 *  4. 记录可信性判定不含"可由 `intent` 证明的提交未完成窗口"：那类窗口按第 1 条处理。
 *  5. `new` 的选择来源（`--plugin` / `--empty` / 交互）由 CLI 展开后经 `selection_override` 交给本函数，
 *     因此 P0 不必回头解析命令行。
 *  6. `--check` 分两段取只读快照：登记文件在 `load_registry` **之前**取样，项目内状态文件（选择、两份记录、
 *     `intent`、根构建描述；`build` 另含 `build.json`）在它们被读取**之前**取样，两段追加进同一对
 *     `snapshot_targets` / `snapshot_before`。复验由编排层在计划与逐项结论产出后调用 `compare_snapshot`
 *     完成：不取锁的只读路径靠这一对取样证明判定期间状态未变（spec §16 第 17 条、§10.12）。
 *     真实执行路径不取样，它有项目锁与锁内基线复验。
 */

namespace ID::Project
{
    /**
     *  一次预检的请求。
     */
    struct PreflightRequest
    {
        CommandKind           command              = CommandKind::Update;
        std::string           project_name         = {};     // CLI 给出或由登记反查
        std::string           project_path_text    = {};     // `new --path` 原文；其它命令为空
        bool                  is_build_requested   = false;  // 命令带 --build，或命令就是 build
        bool                  is_check             = false;
        bool                  has_force            = false;
        bool                  has_restore_action   = false;  // recover 的 --action 是 restore
        std::filesystem::path engine_root          = {};     // `Paths.IDPath` 的父目录
        std::filesystem::path registry_path        = {};
        std::filesystem::path plugin_root          = {};     // <工程根>/IDPlugin

        SelectionFile         selection_override   = {};     // 仅 new：CLI 展开的直接选择
        bool                  has_selection_override = false;
    };

    /**
     *  已读到的记录与事务状态。`is_committed_window` / `is_content_complete` 是 U5 §5.8 的结论，
     *  供编排层决定"只收尾""可续做"或"内容窗口"。
     */
    struct RecordReadings
    {
        bool           has_resolved        = false;
        bool           has_manifest        = false;
        bool           has_intent          = false;
        bool           is_committed_window = false;   // resolved 当前摘要 == intent.target_resolved.digest
        bool           is_content_complete = false;   // 所有内容项当前摘要 == 该项 after
        bool           is_intent_window    = false;   // 两份记录的错配已由 intent 快照证明（提交未完成窗口）
        bool           is_trusted          = false;   // 两份记录一致且自洽
        ResolvedRecord resolved            = {};
        ManifestRecord manifest            = {};
        std::string    resolved_bytes      = {};      // 读到的原始字节，供计划比较记录摘要
        std::string    manifest_bytes      = {};      // 用于校验 manifest_digest，也供计划比较记录摘要
        IntentRecord   intent              = {};
    };

    /**
     *  一次预检的结论。
     */
    struct PreflightResult
    {
        bool                        is_ok                 = false;
        CommandKind                 command               = CommandKind::Update;
        std::filesystem::path       project_root          = {};
        std::filesystem::path       engine_root           = {};    // 复制来源的根：源插件库在它下面
        std::string                 project_name          = {};
        std::string                 selection_bytes_digest = {};   // plugins.json 原始字节摘要；不存在为空
        bool                        has_selection         = false;  // 有可用的直接选择（文件或 CLI 展开）
        RegistryFile                registry              = {};
        const RegistryEntry*        entry                 = nullptr;
        SelectionFile               selection             = {};
        PluginCatalog               catalog               = {};
        RecordReadings              records               = {};
        DiagnosticSet               diagnostics           = {};

        // `--check` 的只读快照：读前取样，计划与逐项结论产出后由编排层复验（spec §16 第 17 条）
        std::vector<SnapshotTarget> snapshot_targets      = {};    // 受检目标，顺序固定
        std::vector<std::string>    snapshot_before       = {};    // 与 targets 一一对应；空串表示不存在
    };

    /**
     *  @brief P0：参数、登记、路径身份、输入可用性、记录可信性、未完成事务
     *  @param request 预检请求
     *  @return 判定结论；`is_ok` 为假时 `diagnostics` 里至少有一条非 warn 诊断
     *  @note 检查项与类别（spec §8.3、§13.2）：项目名形态 → `project_name_invalid`；登记找不到 →
     *        `project_not_found`；路径非绝对或不存在（`new` 除外）→ `project_path_invalid`；非空且未登记 →
     *        `project_exists`（`--force` 不放行）；与工程根相等或互相包含、落在 `ID`/`IDPlugin`/`Benchmark`
     *        子树内 → `project_path_overlap`；卷根、用户主目录、工作台根 → `project_path_invalid`；
     *        `update`/`build` 缺 `plugins.json` → `project_config_missing`；`build` 无已提交记录 →
     *        `project_not_initialized`；两份记录不一致 → `record_invalid`；未完成的 `intent` 按 U5 §5.8
     *        给出 `recovery_conflict`；`is_initialized` 与记录不符 → warn `registry_marker_stale`；
     *        `build`/已提交的 `new` 的选择与校验快照不一致 → `stale_generated`
     *  @note `build` 额外核对 `manifest` 每个条目与磁盘一致（条目缺失 → `record_invalid`、摘要不符 →
     *        `unmanaged_content`），并按 spec §8.3 拒绝任何未完成的 `intent`
     *  @note `manifest` 条目先逐条按 `normalize_relative` 判形态（越界、盘符、`..`、ADS、保留名 →
     *        `path_outside_project`，1），再做 `entries_digest` 与 `generation_id` 的自洽判定：
     *        安全类校验不被摘要校验掩盖；路径全合法时仍按 `record_invalid`（2）报
     */
    PreflightResult run_preflight(const PreflightRequest& request);

    /**
     *  离线选择复核的结果：`digest` 是当前 `plugins.json` 的规范化有效选择摘要。
     */
    struct OfflineSelection
    {
        bool                             is_ok      = false;
        Digest                           digest     = {};
        std::optional<ProjectDiagnostic> diagnostic = {};
    };

    /**
     *  @brief 只用 `resolved.snapshot` 重建"已接受的清单视图"，展开当前选择并算规范化摘要
     *  @param selection 当前 `plugins.json` 的直接选择
     *  @param resolved 已提交记录（提供清单校验快照）
     *  @return 摘要与失败诊断；不读源插件目录、不读 `plugins.json` 原始字节
     *  @note 校验快照是已接受源版本的派生副本（spec §6.2）：`build` 与 `recover` 的前置判定都只用它，
     *        因此源插件目录不可用时不失败
     */
    OfflineSelection evaluate_offline_selection(const SelectionFile& selection, const ResolvedRecord& resolved);

    /**
     *  @brief build 的离线选择复核：规范化有效选择 vs resolved 的校验快照
     *  @param selection 当前直接选择
     *  @param resolved 已提交记录
     *  @return 不一致时 `stale_generated`（1），对象写明哪个插件、哪个字段（linkage / 选项名 / 直接选择集合）
     *  @note 只比较语义值：键序、缩进、显式写出与快照等值的 default 都不报（spec §8.3）
     */
    std::optional<ProjectDiagnostic> check_offline_selection(const SelectionFile& selection,
                                                             const ResolvedRecord& resolved);
} // namespace ID::Project
