#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 *  `resolved.json`：已提交 generation 的记录（spec §6.2，字段分组与六个组逐项对应）。
 *
 *  本头只是载体：谁该写、写失败怎么收敛属于计划与执行阶段。记录里的路径一律相对项目根；
 *  摘要一律写 64 位小写十六进制文本。
 *
 *  不变量：
 *  1. `version` 写空串表示"清单无版本字段"的模型缺口，不填伪值。
 *  2. `selection_bytes_digest` 只用于 TOCTOU 与复核，不参与 `generation_id`；语义判据是
 *     `selection_digest`（规范化有效选择摘要）。
 *  3. `retention` 是路径级集合：受控片段只贡献所属文件路径一次，根 `CMakeLists.txt` 无条件在其中。
 *  4. `limitations` 首条固定 `g8_engine_option_not_projected`：公开行为限制，不是错误，不改变退出码。
 *  5. 记录里不出现计划摘要字段，也不出现构建 profile。
 *  6. `targets[].option_vars` 与 `targets[].option_defines` 同长度同序（按选项名排序），
 *     未声明 `defines` 的选项在 `option_defines` 里是空串占位，不是缺项。
 */

namespace ID::Project
{
    /**
     *  清单选项快照：已接受源版本的派生副本，不是第二份元数据真源。
     */
    struct ResolvedOptionSnapshot
    {
        std::string                name          = {};
        std::string                type          = "bool";   // v1 固定
        bool                       default_on    = false;
        std::string                description   = {};
        std::optional<std::string> define        = {};
        std::vector<std::string>   links_when_on = {};
    };

    /**
     *  一个被选插件的清单元数据快照。
     */
    struct ResolvedManifestSnapshot
    {
        std::string                          plugin          = {};
        std::string                          manifest_digest = {};   // 清单原始字节摘要
        std::string                          version         = {};   // 清单无版本字段：固定空串
        std::vector<std::string>             dependencies    = {};
        std::vector<std::string>             requirements    = {};   // 原样保留，"SHARED_ONLY"/"STATIC_ONLY"/空
        std::vector<ResolvedOptionSnapshot>  options         = {};   // 按选项名排序
    };

    /**
     *  闭包内一个插件（直接项或补齐项）的有效选择。
     */
    struct ResolvedSelectionEntry
    {
        std::string                plugin          = {};
        std::string                linkage         = {};   // "static" / "dynamic"
        std::string                linkage_source  = {};   // "explicit" / "requirement"
        bool                       is_direct       = false;
        std::vector<std::string>   option_names    = {};   // 与 option_values / option_sources 同下标
        std::vector<std::string>   option_values   = {};
        std::vector<std::string>   option_sources  = {};   // "explicit" / "default"
    };

    /**
     *  一条依赖边：`controlling_options` 为空表示无条件链接。
     */
    struct ResolvedEdge
    {
        std::string              from                = {};
        std::string              to                  = {};
        std::vector<std::string> controlling_options = {};
    };

    /**
     *  一个 include 根（相对项目根）；`is_system` 为真表示三方头，CMake 侧 SYSTEM PUBLIC。
     */
    struct ResolvedIncludeRoot
    {
        std::string path      = {};
        bool        is_system = false;
    };

    /**
     *  一条共享 vendor 链接。
     */
    struct ResolvedVendorLink
    {
        std::string dir    = {};   // 共享 vendor 目录名，落 IDPlugin/Vendor/<dir>
        std::string target = {};   // 被本插件链接的 CMake 目标名
    };

    /**
     *  一个插件的目标与变量：生成侧接线与记录复核共用。
     *
     *  `option_vars` 与 `option_defines` **同长度、同序**：下标对应同一个选项，顺序按**选项名**的 UTF-8
     *  字节序；某选项未声明 `defines` 时 `option_defines` 该位为空串（与 `PluginNaming` 同口径）。
     */
    struct ResolvedTargetEntry
    {
        std::string                       plugin          = {};
        std::string                       target          = {};
        std::string                       target_type     = {};   // "STATIC" / "SHARED"
        std::vector<std::string>          option_vars     = {};   // ID<TAG>_<OPTION>，按选项名排序
        std::vector<std::string>          option_defines  = {};   // 清单 defines；未声明为 ""，与 option_vars 同下标
        std::string                       static_macro    = {};
        std::string                       export_macro    = {};
        std::vector<std::string>          source_dirs     = {};   // 相对项目根，按字节序
        std::vector<ResolvedIncludeRoot>  include_roots   = {};   // 按 path 字节序
        std::vector<ResolvedVendorLink>   vendor_links    = {};   // 按 (dir, target) 排序
    };

    /**
     *  一条交付资产。
     */
    struct ResolvedAssetEntry
    {
        std::string                path     = {};
        std::optional<std::string> fragment = {};   // 写出恒为 null；读取只容忍旧记录出现
        std::optional<std::string> source   = {};   // 相对工程根；生成物为空
        std::string                digest   = {};
        std::string                stage    = {};   // "copy" / "generate"
        std::optional<std::string> plugin   = {};
    };

    /**
     *  一条公开行为限制：不是错误，不改变退出码。
     */
    struct ResolvedLimitation
    {
        std::string code    = {};   // 稳定标识，如 "g8_engine_option_not_projected"
        std::string message = {};   // 中文一句话说明
    };

    /**
     *  `resolved.json` 的完整内容（键序与写出顺序见 ResolvedJson.cpp）。
     */
    struct ResolvedRecord
    {
        // 身份
        std::string schema_version  = {};
        std::string rules_version   = {};
        std::string generation_id   = {};
        std::string project_name    = {};
        std::string manifest_digest = {};

        // 输入
        std::string selection_digest        = {};   // 规范化有效选择摘要
        std::string selection_bytes_digest  = {};   // plugins.json 原始字节摘要，仅作复核身份

        // 校验快照（按插件名排序）
        std::vector<ResolvedManifestSnapshot> snapshot = {};

        // 选择（闭包全集，按插件名排序）
        std::vector<ResolvedSelectionEntry> selection = {};

        // 依赖边（按 (from, to) 排序）
        std::vector<ResolvedEdge> edges = {};

        // 顺序（被依赖者在前）
        std::vector<std::string> init_order = {};

        // 目标与变量（按 plugin 排序）
        std::vector<ResolvedTargetEntry> targets = {};

        // 资产（按 path 排序，不含受控片段）
        std::vector<ResolvedAssetEntry> assets = {};

        // 保留集（按路径排序；含目录与根 CMakeLists.txt 路径，片段不另立条目）
        std::vector<std::string> retention = {};

        // 公开行为限制（只读；不是错误，不改变退出码）
        std::vector<ResolvedLimitation> limitations = {};
    };
} // namespace ID::Project
