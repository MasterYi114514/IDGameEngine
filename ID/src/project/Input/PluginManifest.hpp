#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Input/Linkage.hpp"

/**
 *  插件清单 `plugin_config.json` 的强类型模型与语法/自洽校验（契约源：`skills/id-plugin/SKILL.md` §4）。
 *
 *  只做"格式合法"与"清单内部自洽"的判定：不检查条目在插件目录里是否真实存在（那是资产完整性）、
 *  不推导 TAG、不合并选项、不判断依赖能否闭合。解析失败按 `warn` 级 `plugin_config_invalid` 报告并
 *  由目录扫描方把该插件移出可选列表，不改变退出码。
 *
 *  不变量：
 *  1. 十项字段逐项校验，未知字段一律拒绝（`skills/id-kernel/SKILL.md` 第 6 节第 2 步）；缺任一必填字段
 *     即失败，不存在"默认补空数组"的宽容读取。
 *  2. `name` 必须等于所在目录名；`dependencies` 不得含自身或 `Common`；`requirements` 最多 1 项。
 *  3. `header_dirs` 的条目种类由条目之间的包含关系判定：落在同一清单另一个条目之下的为单文件条目，
 *     其余为目录条目——解析层不访问磁盘，无法用文件类型判别。
 *  4. `manifest_digest` 覆盖清单原始字节（含缩进），作者改格式也会改变 generation 身份。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  `header_dirs` 条目的种类。目录条目是 include 根并整树复制；单文件条目只收敛复制与保留范围。
     */
    enum class HeaderDirKind : std::uint8_t
    {
        Directory = 0,   // 目录本身就是 include 根
        File      = 1,   // 单文件条目：落在某个目录条目之下，不新增 include 根
    };

    struct HeaderDirEntry
    {
        std::string    path = {};                       // 相对插件根，POSIX 分隔
        HeaderDirKind  kind = HeaderDirKind::Directory;  // 条目种类
    };

    /**
     *  中央 `IDPlugin/Vendor/` 下的共用三方目录。
     */
    struct SharedVendor
    {
        std::string                dir    = {};     // 目录名，不含分隔符
        std::optional<std::string> target = {};     // 主要目标名；空表示只复制
    };

    /**
     *  一个可选项声明（v1 只有 bool）。
     */
    struct ManifestOption
    {
        std::string                name          = {};      // ^[A-Z][A-Z0-9_]*$，不带 ID<TAG>_ 前缀
        bool                       is_bool       = true;    // `type` 字段存在且为 "bool"
        bool                       default_on    = false;   // `default` 的布尔解释
        std::string                description   = {};      // 进 option() 的描述位
        std::optional<std::string> define        = {};      // 宏名，^[A-Z][A-Z0-9_]*$
        std::vector<std::string>   links_when_on = {};      // 必须 ⊆ dependencies
    };

    /**
     *  一份清单的只读快照：解析后不再回读源文件。
     */
    struct PluginManifest
    {
        std::string                     name            = {};
        std::vector<std::string>        dependencies    = {};
        Requirement                     requirement     = Requirement::Any;
        std::vector<HeaderDirEntry>     header_dirs     = {};
        std::vector<std::string>        common_headers  = {};   // 相对 IDPlugin/Common
        std::vector<std::string>        source_dirs     = {};
        std::vector<std::string>        vendor_dirs     = {};
        std::vector<SharedVendor>       shared_vendors  = {};
        std::vector<std::string>        files           = {};
        std::vector<ManifestOption>     options         = {};

        std::string                     source_root     = {};   // 插件根相对工程根，如 "IDPlugin/IDJson"
        std::string                     manifest_digest = {};   // 清单原始字节摘要（小写十六进制）
    };

    /**
     *  一次解析的结果：is_ok 为假时 diagnostic 是 warn 级 plugin_config_invalid（含原始忽略原因）。
     */
    struct ManifestParse
    {
        bool                             is_ok      = false;
        PluginManifest                   manifest   = {};
        std::optional<ProjectDiagnostic> diagnostic = {};
    };

    /**
     *  @brief 解析一份清单的原始字节
     *  @param bytes 文件原始字节（摘要由本函数用同一份字节计算）
     *  @param directory_name 清单所在目录名，必须等于清单 `name`
     *  @return 解析结果；失败时 diagnostic 的 actual 写明原始忽略原因
     *  @note 判定范围：缺必填字段、类型不符、顶层非对象、未知字段、name 与目录名不符、
     *        dependencies 含自身或 "Common"、requirements 取值不在取值集或长度 > 1、header_dirs 条目
     *        为绝对路径/含 '..'/为空、单文件条目的父目录条目不存在、common_headers 与其它条目同一条
     *        相对形态校验（空、绝对路径、盘符、含 '..' 一律失败）且不得是目录形态、
     *        shared_vendors[].dir 含分隔符或为 ".."、target 为空串、options 的各项规则
     *        （`name` 空串与非法字符同样按 `^[A-Z][A-Z0-9_]*$` 失败）
     *  @note 不访问磁盘、不判断条目是否存在
     */
    ManifestParse parse_manifest(std::string_view bytes, std::string_view directory_name);
} // namespace ID::Project
