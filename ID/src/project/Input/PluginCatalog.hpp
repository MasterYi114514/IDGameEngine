#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Input/PluginManifest.hpp"

/**
 *  插件发现：扫描 `<工程根>/IDPlugin/<Plugin>/plugin_config.json`，两遍成表（清单解析 + 依赖引用检查）。
 *
 *  发现阶段只回答"有哪些可用插件、哪些被忽略以及为什么"：不做闭包、不判相容性、不读资产内容，
 *  也不因为某个插件的上游缺失而改动它的选择。被忽略的插件在后续阶段由 Resolve 报 `plugin_unknown`
 *  并附本文件记下的原始原因。
 *
 *  不变量：
 *  1. `Common` 与 `Vendor` 不参与发现；目录内没有清单的目录不参与发现（不是错误）。
 *  2. 清单格式非法 → 该插件整体忽略并记一条 warn `plugin_config_invalid`；依赖引用未发现的插件 →
 *     该插件整体忽略并记一条 warn `dependency_unknown`。两者都不改变退出码。
 *  3. `entries` 与 `ignored` 都按名字的 UTF-8 字节序排序：目录列举顺序不参与任何决定。
 *  4. 只读、不跟随重解析点；目录不可读时 `is_ok` 为假，由调用方按输入环境失败处理。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  一个已发现的插件：清单快照 + 清单字节数（仅用于诊断展示）。
     */
    struct CatalogEntry
    {
        PluginManifest  manifest = {};
        std::uint64_t   bytes    = 0;
    };

    /**
     *  一次发现的完整结果。
     */
    struct PluginCatalog
    {
        bool                        is_ok       = false;    // 扫描本身是否成功（能读目录）
        std::vector<CatalogEntry>   entries     = {};       // 按插件名升序
        DiagnosticSet               diagnostics = {};       // warn：plugin_config_invalid、dependency_unknown
        std::vector<std::string>    ignored     = {};       // 被忽略的插件名，按字节序
    };

    /**
     *  @brief 扫描插件目录
     *  @param id_plugin_root `<工程根>/IDPlugin` 的绝对路径
     *  @return 发现结果；目录不可读时 is_ok 为假且不带条目
     *  @note 只取一级子目录，跳过 `Common` 与 `Vendor`；目录名与清单 `name` 不符按清单非法忽略
     *  @note 依赖引用检查在全部清单解析完成后进行：引用未发现插件的插件整体进入 ignored
     */
    PluginCatalog discover_plugins(const std::filesystem::path& id_plugin_root);

    /**
     *  @brief 按插件名查找已发现的插件
     *  @return 命中时返回条目指针，否则返回 nullptr
     */
    const CatalogEntry* find_plugin(const PluginCatalog& catalog, std::string_view name) noexcept;

    /**
     *  `plugin_unknown` 的补充原因：区分"不存在"与"存在但被忽略"。
     */
    struct UnknownPluginReason
    {
        bool        is_ignored      = false;
        std::string original_reason = {};   // 来自被忽略清单的原始原因文本，可为空
    };

    /**
     *  @brief 解释某个插件名为什么不可用
     *  @param catalog 发现结果
     *  @param name 插件名
     *  @return 被忽略时 is_ignored 为真并附原始原因；未发现时 is_ignored 为假
     */
    UnknownPluginReason explain_unknown(const PluginCatalog& catalog, std::string_view name);
} // namespace ID::Project
