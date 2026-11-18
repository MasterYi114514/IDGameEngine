#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Input/Linkage.hpp"
#include "project/Input/PluginCatalog.hpp"
#include "project/Input/Selection.hpp"

/**
 *  有效选择：把用户直接选择与清单缺省值展开成"写全"的形态。
 *
 *  展开后每个插件都有具体的 `linkage`、清单声明的每一个选项（含用户没写出的）与取值来源标记，
 *  因此工具补写初值不会产生假差异：`plugins.json` 是否显式写出与缺省等值的取值不影响后续判定。
 *
 *  不变量：
 *  1. 选项取值优先级（spec §10.2）：`plugins.json` 显式 > 清单 `default`；直接项的 `linkage` 只来自
 *     `linkage_type`，由 `requirements` 推出的缺省形态只用于闭包补齐项。
 *  2. 取值来源（OptionSource / LinkageSource）只出现在本结构与 `resolved` 记录里，不进入任何摘要。
 *  3. 插件与选项一律按名字的 UTF-8 字节序排序，用户书写顺序与清单声明顺序不进入产物。
 *  4. 选项描述原样取自展开时那份清单快照（`options[].description`），不额外读盘，也不进入任何摘要。
 *  5. 本单元只做纯计算：不写盘、不取锁、不读第二遍源文件。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  选项生效值的来源。
     */
    enum class OptionSource : std::uint8_t
    {
        Explicit = 0,   // plugins.json 显式写出的取值
        Default  = 1,   // 清单 default
    };

    /**
     *  链接形态的来源。
     */
    enum class LinkageSource : std::uint8_t
    {
        Explicit    = 0,   // plugins.json 的 linkage_type（直接项必填）
        Requirement = 1,   // 由 requirements 推出的缺省形态（只用于闭包补齐项）
    };

    struct EffectiveOption
    {
        std::string   name        = {};                    // 选项名，不带前缀
        bool          value       = false;                 // bool 取值；v1 只有 bool
        OptionSource  source      = OptionSource::Default;  // 取值来源
        std::string   description = {};                    // 清单 options[].description 原文，进 option() 的描述位
    };

    struct EffectivePlugin
    {
        std::string                  name           = {};
        Linkage                      linkage        = Linkage::Static;
        LinkageSource                linkage_source = LinkageSource::Requirement;
        std::vector<EffectiveOption> options        = {};      // 按选项名排序，含清单声明的全部选项
        bool                         is_direct      = false;   // 是否由用户直接选择
    };

    /**
     *  展开结果。`direct` 是本函数产出的直接选择项；`all` 是闭包全集，由 resolve_closure 在补齐
     *  依赖项后填满（本函数把 `all` 置为与 `direct` 相同，表示"尚未补齐"）。
     */
    struct EffectiveSelection
    {
        std::string                  project_name = {};
        std::vector<EffectivePlugin> direct       = {};      // 直接选择项，按插件名升序
        std::vector<EffectivePlugin> all          = {};      // 闭包全集，按插件名升序
        DiagnosticSet                diagnostics  = {};
        bool                         is_valid     = false;
    };

    /**
     *  @brief 展开直接选择项：补齐 linkage 与全部选项的生效值
     *  @param selection 已通过结构校验的选择
     *  @param catalog 插件目录
     *  @return 展开结果；未发现或被忽略的插件按 plugin_unknown 记诊断并移出直接项
     *  @note 直接项与 requirements 冲突报 requirement_conflict；未声明选项报 option_unknown；
     *        取值不在 bool 合法集报 option_value_invalid
     *  @note 只读：不读磁盘、不改输入
     */
    EffectiveSelection expand_direct_selection(const SelectionFile& selection, const PluginCatalog& catalog);

    /**
     *  @brief 按清单缺省值展开一个插件（闭包补齐项的构造入口）
     *  @param manifest 清单快照
     *  @param is_direct 是否由用户直接选择
     *  @return 带具体 linkage（由 requirements 推出）与全部选项（source = Default）的有效插件
     *  @note 直接项不走本函数：它们的取值来自 `plugins.json`，由 expand_direct_selection 给出
     */
    EffectivePlugin expand_manifest_defaults(const PluginManifest& manifest, bool is_direct);

    /**
     *  @brief 取某个选项的生效值
     *  @param plugin 已展开的插件
     *  @param option_name 选项名
     *  @param out 取值；命中时被覆盖
     *  @return 该插件声明了此选项时返回 true
     */
    bool find_option_value(const EffectivePlugin& plugin, std::string_view option_name, bool& out) noexcept;
} // namespace ID::Project
