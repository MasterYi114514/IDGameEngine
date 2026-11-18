#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Digest.hpp"
#include "project/Resolve/EffectiveSelection.hpp"

/**
 *  generation 身份：`generation_id`、规范化有效选择摘要与规范化计划摘要。
 *
 *  身份只由 `GenerationContent` 的五个输入决定：规则标识、项目名、规范化有效选择摘要、每个被选插件的
 *  清单摘要、全部交付资产的内容摘要。不含时间戳、本机路径、遍历顺序、构建 profile，也不含任何计划
 *  摘要——删除集合与按磁盘状态裁剪出的动作集不是身份输入，同终态必然同身份。
 *
 *  不变量：
 *  1. 全部字段只经 `Sha256::update_field` / `update_tag` 追加：拼接字符串后再哈希会让
 *     "ab"+"c" 与 "a"+"bc" 同摘要。
 *  2. 片段条目（`fragment` 非空）的摘要只有一个计算入口：`ID::CMake::fragment_range_digest`；
 *     本单元只用调用方给的值，不自行拼接片段文本再哈希。
 *  3. `rules_version` 与规则变更是同一次提交：生成文本规则、记录 schema、保留集口径任一变化都必须提升。
 *  4. 本单元只做纯计算：不读文件、不写盘、不取锁。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    // 生成规则标识：生成文本规则、记录 schema 或保留集口径变化时必须提升
    inline constexpr std::string_view kRulesVersion = "id-project/1";

    /**
     *  一个交付资产的内容摘要条目。`fragment` 为空表示普通文件条目。
     */
    struct ContentDigest
    {
        std::string                path     = {};
        std::optional<std::string> fragment = {};   // 空表示普通文件条目
        Digest                     digest   = {};
    };

    /**
     *  generation 身份的五个输入。
     */
    struct GenerationContent
    {
        std::string                                  rules_version     = {};
        std::string                                  project_name      = {};
        Digest                                       selection_digest  = {};   // 规范化有效选择摘要
        std::vector<std::pair<std::string, Digest>>  manifest_digests  = {};   // (插件名, 清单摘要)，按名字排序
        std::vector<ContentDigest>                   delivered         = {};   // 复制 + 生成，按 (path, fragment) 排序
    };

    /**
     *  @brief 计算 generation_id
     *  @param content 五个输入
     *  @return 64 位小写十六进制
     *  @note 编码（唯一口径）：tag "id-project-generation" → field(rules_version) → field(project_name)
     *        → field(selection_digest 十六进制) → 每个插件 tag "manifest" + field(name) + field(digest 十六进制)
     *        → 每个交付资产：普通文件 tag "entry/file"，片段 tag "entry/fragment" + field(fragment)，
     *        然后 field(path) + field(digest 十六进制)
     */
    std::string compute_generation_id(const GenerationContent& content);

    /**
     *  @brief 规范化有效选择摘要
     *  @param selection 展开后的有效选择（闭包全集）
     *  @return 摘要值
     *  @note 编码：tag "id-project-selection"；对每个插件（按名字）tag "plugin"、field(name)、
     *        field(linkage 文本)、field(is_direct ? "direct" : "dependency")、field(选项数量)，
     *        再对每个选项（按名字）field(name)、field("ON" | "OFF")
     *  @note 取值来源（OptionSource）不进入编码：它只进 resolved 记录与人读输出
     */
    Digest compute_selection_digest(const EffectiveSelection& selection);

    /**
     *  @brief 规范化计划摘要
     *  @param writes 计划写入项（含记录文件以外的一切内容），按 (path, fragment) 排序后参与
     *  @param deletes 计划删除项，按 (path, fragment) 排序后参与
     *  @return 摘要值
     *  @note 编码：tag "id-project-plan"；写入项 tag "write" + field(path) + field(fragment)，
     *        删除项 tag "delete" + field(path) + field(fragment)
     *  @note 只服务 `intent` 与 `--check` 的差异说明：不进入 `generation_id`，也不写成 `resolved` 字段
     */
    Digest compute_plan_digest(std::span<const ContentDigest> writes, std::span<const ContentDigest> deletes);
} // namespace ID::Project
