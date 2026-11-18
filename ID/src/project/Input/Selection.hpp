#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Input/Linkage.hpp"

/**
 *  项目 `plugins.json`：用户对插件、链接形态与选项的**直接选择**。
 *
 *  只做结构校验与序列化：未知插件、选项名是否存在、取值是否属于该选项的合法集、链接形态与作者约束
 *  是否相容，分别由 Resolve/EffectiveSelection 判定（前者需要清单）。文件不存在是正常状态
 *  （is_present 为假），由命令层决定报 project_config_missing 还是走首次写出路径。
 *
 *  不变量：
 *  1. 严格字段表：未知字段一律拒绝，不"忽略未知键"；`linkage_type` 与 `options` 缺键即结构非法，
 *     读取既有文件时没有"按 requirements 推导缺省形态"的宽容展开（那只发生在 CLI 落盘前）。
 *  2. 顺序无语义：本单元保留文件顺序只为诊断定位，任何判定都不得依赖它。
 *  3. 序列化键序固定 name/plugins，每项 name/linkage_type/options，选项键按名字字节序，
 *     4 空格缩进、以 '\n' 结尾；与读取互为逆运算（写出后立即读回得到同一组直接选择项）。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  选项赋值：`plugins[].options` 的一项。取值只存原文，合法集判定留给解析阶段。
     */
    struct OptionAssignment
    {
        std::string name  = {};     // 选项名，不带 ID<TAG>_ 前缀
        std::string value = {};     // 原始取值文本（"ON" / "OFF"）
    };

    /**
     *  一条直接选择。`linkage` 只来自文件必填的 `linkage_type`；NSDMI 只是初值。
     */
    struct SelectionPlugin
    {
        std::string                   name     = {};                // 插件名
        Linkage                       linkage  = Linkage::Static;   // 文件必带 linkage_type
        std::vector<OptionAssignment> options  = {};                // 文件必带 options 键，可为空对象
    };

    /**
     *  一次读取的结果。文件不存在时 is_present 与 is_valid 都为假，由调用方按命令给类别。
     */
    struct SelectionFile
    {
        bool                         is_present  = false;   // 文件是否存在
        bool                         is_valid    = false;   // 结构是否可用
        std::string                  name        = {};      // 必须等于登记的项目名
        std::vector<SelectionPlugin> plugins     = {};      // 直接选择项，顺序无语义
        DiagnosticSet                diagnostics = {};      // 非法时的类别与对象
    };

    /**
     *  @brief 读取 `<项目根>/plugins.json`
     *  @param path 选择文件绝对路径
     *  @return 读取结果；文件不存在时 is_present 与 is_valid 都为假且不带诊断
     *  @note 必填键：`name`、`plugins`、每个 `plugins[].name`、`plugins[].linkage_type`、`plugins[].options`
     *        （`options` 允许空对象，但不允许缺键）
     *  @note 违规映射：解析失败 / 顶层非对象 / 缺必填键 / 类型不符 / 未知字段 / `linkage_type` 非
     *        static|dynamic / `options` 非对象或取值非字符串 → project_config_invalid；
     *        同一插件出现两次 → plugin_duplicate
     */
    SelectionFile load_selection(const std::filesystem::path& path);

    /**
     *  @brief 首次写出：由已展开有效值的直接选择项生成文本
     *  @param project_name 必须与登记名一致
     *  @param plugins 按拓扑序排列的直接选择项（每项都带显式 linkage 与全部选项取值）
     *  @return 4 空格缩进、键序 name/plugins、每项键序 name/linkage_type/options、以 '\n' 结尾的文本
     */
    std::string serialize_selection(std::string_view project_name, std::span<const SelectionPlugin> plugins);

    /**
     *  @brief 校验选择文件与项目名的对应关系
     *  @param selection 已读取的选择
     *  @param project_name 登记的项目名
     *  @return 不一致时给出 project_name_mismatch（对象为两个名字，下一步为改正任一侧）；一致或文件不存在时为空
     */
    std::optional<ProjectDiagnostic> check_selection_name(const SelectionFile& selection,
                                                          std::string_view project_name);
} // namespace ID::Project
