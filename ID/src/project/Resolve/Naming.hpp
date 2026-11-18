#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Input/PluginCatalog.hpp"
#include "project/Resolve/Closure.hpp"

/**
 *  TAG、目标名与全局符号：命名只推导、不 sanitize，冲突在计划阶段一次报全。
 *
 *  TAG = 插件名去掉前导 "ID" 后转 ASCII 大写（`IDJson` → `JSON`）；选项变量 `ID<TAG>_<OPTION>`、
 *  形态宏 `ID_<TAG>_STATIC` / `ID_<TAG>_EXPORTS` 与清单 `options[].defines` 共用同一 TAG 推导。
 *
 *  不变量：
 *  1. 不改写任何名字：折叠后不合法就报 `cmake_name_conflict`，不由工具替换字符或改公开宏约定。
 *  2. 保留名比较按大小写不敏感（Windows 上会生成两个无法区分的符号），覆盖 CMake 内建目标与生成器
 *     自带宏/变量。
 *  3. 每个冲突一条诊断：对象写明冲突符号，期望写明唯一性/合法性，实际写明两侧来源，下一步写明
 *     "v1 消费模型限制"与需要改哪一侧。
 *  4. 本单元无写盘与锁操作。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  一个插件的派生名集合（项目段用同一结构，`tag` 为空）。
     *
     *  `option_vars`、`option_defines`、`option_descriptions` 三者**同长度、同序**：下标对应同一个选项，
     *  顺序按**选项名**的 UTF-8 字节序（不是宏名）。某选项未声明 `defines` 时 `option_defines` 该位为空串。
     */
    struct PluginNaming
    {
        std::string              plugin              = {};
        std::string              tag                 = {};   // 插件名去前导 "ID" 后 ASCII 大写
        std::string              target              = {};   // CMake 目标名 = 插件名
        std::vector<std::string> option_vars         = {};   // ID<TAG>_<OPTION>，按选项名排序
        std::vector<std::string> option_defines      = {};   // 清单 defines；未声明为 ""，与 option_vars 同下标
        std::vector<std::string> option_descriptions = {};   // 清单 description 原文，与 option_vars 同下标
        std::string              static_macro        = {};   // ID_<TAG>_STATIC
        std::string              export_macro        = {};   // ID_<TAG>_EXPORTS
    };

    /**
     *  命名计划：项目段、逐插件派生名、汇总目标与共享 vendor 目标。
     */
    struct NamingPlan
    {
        PluginNaming              project        = {};      // target 为项目名，tag 为空
        std::vector<PluginNaming> plugins        = {};      // 按插件名排序
        std::string               common_target  = {};      // 固定 "IDCommonHeaders"
        std::vector<std::string>  vendor_targets = {};      // shared_vendors 中 target 非空的目标名
        DiagnosticSet             diagnostics    = {};
        bool                      is_valid       = false;
    };

    /**
     *  @brief 推导 TAG
     *  @param plugin_name 插件名
     *  @return 去掉前导 "ID" 后转 ASCII 大写；名字没有 "ID" 前缀时整体大写；空名返回空串
     *  @note 只处理 ASCII 大小写，不做其它改写；结果是否合法由 check_identifier 判定
     */
    std::string make_tag(std::string_view plugin_name);

    /**
     *  @brief CMake/C++ 标识符形态判定（^[A-Za-z_][A-Za-z0-9_]*$）
     */
    bool check_identifier(std::string_view text) noexcept;

    /**
     *  @brief 项目名与插件目标的字符规则判定（^[A-Za-z][A-Za-z0-9_-]*$）
     */
    bool check_project_name(std::string_view text) noexcept;

    /**
     *  @brief 是否是 CMake 内建目标名（大小写不敏感）
     *  @note 集合：all、ALL_BUILD、clean、help、install、package、package_source、test、RUN_TESTS、
     *        rebuild_cache、edit_cache、ZERO_CHECK
     */
    bool is_reserved_target(std::string_view text) noexcept;

    /**
     *  @brief 是否是生成器自带符号（大小写不敏感）
     *  @note 集合：宏 `ID_DEBUG`、`ID_RELEASE`；变量 `ID_OUTPUT_ROOT`、`ID_BUILD_TOOLCHAIN`、`ID_BUILD_ARCH`
     */
    bool is_reserved_generated_symbol(std::string_view text) noexcept;

    /**
     *  @brief 汇总并检查全部全局符号
     *  @param project_name 项目名（同时是 CMake 工程名与可执行目标名）
     *  @param resolution 已解析的组装结果
     *  @param catalog 插件目录（取共享 vendor 目标与清单选项的 defines）
     *  @return 命名计划与全部冲突诊断；任一冲突即为 cmake_name_conflict，计划无效
     *  @note 检查项（spec §2.4）：项目名形态与保留名、插件目标形态与保留名、IDCommonHeaders 不与
     *        任何插件/vendor/项目同名、vendor 目标唯一且不命中保留目标名与生成器自带符号、选项变量唯一、
     *        选项宏唯一、形态宏唯一、TAG 折叠碰撞、全部派生名满足标识符合法性、生成器自带符号未被占用
     */
    NamingPlan plan_naming(std::string_view project_name, const Resolution& resolution,
                           const PluginCatalog& catalog);
} // namespace ID::Project
