#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Input/PluginCatalog.hpp"
#include "project/Resolve/Closure.hpp"

/**
 *  交付资产计划：按清单声明枚举逐文件复制集合与生成集合，并做**清单显式声明**的完整性检查。
 *
 *  复制集合是逐文件的：目录条目也要展开成文件条目，因为 `manifest.json` 只登记内容文件；目录另立
 *  一个集合供保留集使用。工具不扫描 C++ `#include`、不猜交付件：目录里存在但未被任何声明覆盖的文件
 *  既不报错、也不复制、也不进入保留集，归属证据只有清单声明。
 *
 *  不变量：
 *  1. 完整性检查只覆盖清单显式声明的条目与插件根必备交付件：声明条目不存在或类型不符 →
 *     `asset_missing`；插件根缺 `plugin_config.json` 或 `README.md` → `plugin_asset_incomplete`
 *     （同一路径只报其中一个类别，不重复报）。两者都在写入前拒绝。
 *  2. 每条声明路径先判形态、再判重解析点，最后才判存在性：形态违规或祖先链命中链接（指向项目外）
 *     一律 `path_outside_project`；只有"形态合法但不存在或类型不符"才是 `asset_missing`（spec §2.3）。
 *     `header_dirs` 的单文件收敛（spec §10.5）：同一目录条目下存在单文件条目时，该目录只承担
 *     include 根语义，复制与保留范围收敛为被列出的文件集合。
 *  3. 生成项（`AssetStage::Generate`）此阶段 `digest` 为零值，由生成单元回填；回填后 manifest 摘要、
 *     generation 身份与计划写入项三处必须一致。
 *  4. 同一目标路径被两处声明命中：摘要相同合并为一条，不同按 `plugin_asset_incomplete` 报告并写明
 *     两侧来源，不做优先级择一。
 *  5. 本单元只读源资产，不写盘、不取锁、不判定与既有磁盘内容的冲突（那是计划与执行阶段的事）。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  资产条目的来源阶段。
     */
    enum class AssetStage : std::uint8_t
    {
        Copy     = 0,   // 从插件源复制
        Generate = 1,   // 工具生成（内容由生成单元提供）
    };

    /**
     *  一个交付文件：目标相对路径（相对项目根）、来源（相对工程根）与内容摘要。
     */
    struct AssetFile
    {
        std::string                target_relative = {};    // 相对项目根，POSIX
        std::optional<std::string> source_relative = {};    // 相对工程根；生成物为空
        Digest                     digest          = {};    // 复制项为源内容摘要；生成项由生成单元填入
        AssetStage                 stage           = AssetStage::Copy;
        std::optional<std::string> plugin          = {};    // 归属插件名；不属插件时为空
    };

    /**
     *  一个需要创建/保留的目录（保留集素材）。
     */
    struct AssetDirectory
    {
        std::string                target_relative = {};   // 相对项目根的目录路径
        std::optional<std::string> plugin          = {};   // 归属插件名；不属插件时为空
    };

    /**
     *  资产计划：文件集合（按目标路径排序）与目录集合（按目标路径排序）。
     */
    struct AssetPlan
    {
        std::vector<AssetFile>      files       = {};   // 按 target_relative 字节序
        std::vector<AssetDirectory> directories = {};   // 按 target_relative 字节序
        DiagnosticSet               diagnostics = {};
        bool                        is_valid    = false;
    };

    /**
     *  @brief 按清单声明枚举复制集合与生成集合
     *  @param project_root 项目根绝对路径（目标侧检查归计划阶段；本函数只用它确认目标命名空间）
     *  @param engine_root 工程根绝对路径（源资产在 `<工程根>/IDPlugin` 下）
     *  @param resolution 组装结果（闭包、Common 头并集）
     *  @param catalog 插件目录（清单快照）
     *  @return 资产计划；任一 `asset_missing` 或 `plugin_asset_incomplete` 都使 is_valid 为假
     *  @note 枚举范围（spec §8.6 保留集 1–8、10 的文件部分）：`IDPlugin/CMakeLists.txt`（生成）、
     *        每个插件的 `CMakeLists.txt`（生成）、`plugin_config.json` 与 `README.md`（复制）、
     *        `header_dirs`、`common_headers` 并集、`source_dirs`、`vendor_dirs`、`shared_vendors`、
     *        `files`；受控片段与 `src/main.cpp` 不属本计划（片段按 `(path, fragment)` 由计划阶段建条目）
     *  @note 每个目标与来源路径都经 `normalize_relative`（`Base/PathText.hpp` 的唯一入口）；绝对路径、
     *        盘符、`..`、非 POSIX 形态与 Windows 保留名报 `path_outside_project`（码 1），该条目不进入
     *        文件集合与目录集合
     *  @note 只读源资产；不修改、不移动、不删除任何文件
     */
    AssetPlan plan_assets(const std::filesystem::path& project_root, const std::filesystem::path& engine_root,
                          const Resolution& resolution, const PluginCatalog& catalog);
} // namespace ID::Project
