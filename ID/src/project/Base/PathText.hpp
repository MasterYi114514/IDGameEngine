#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/**
 *  路径规范化、越界判定与形态校验。
 *
 *  承载约定：进程内所有项目内路径以 POSIX 分隔的 UTF-8 相对路径字符串保存；只有
 *  std::filesystem::path 用于真正访问磁盘，两者在边界处经本头转换。
 *
 *  不变量：
 *  1. normalize_relative 是项目内路径的唯一入口：manifest 条目、intent 操作项、保留集与复制目标
 *     全部经过它，任何绕过它的直接拼接都属于实现缺陷。
 *  2. 越界与形态违规统一映射为 path_outside_project（spec §2.3），PathVerdict 只决定诊断的
 *     actual 文案，不改变类别。
 *  3. 重解析点检查在写入与删除前各做一次，不做缓存——路径可能在计划与写入之间被替换。
 *  4. 大小写折叠、保留名、重解析点与路径长度只在 Windows 上生效；其它平台按精确字节与自身语义处理，
 *     差异全部集中在 PathText.cpp 的文件级条件编译里。
 *  5. 绝对路径的等价比较只有 `is_same_absolute_path` 一个入口（含 8.3 短名展开）：登记查重、重复登记判定
 *     与登记写入前一律经它，不另写第二份比较，否则同一目录会被登记两次。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  写入目标的地址作用域：两条路径命名空间互不混用。
     *  Project = 项目根内的相对路径；Engine = 工程根的本机登记文件。
     */
    enum class AddressScope : std::uint8_t
    {
        Project = 0,    // 项目根内的相对路径，必须经 normalize_relative
        Engine  = 1,    // 工程根的本机登记文件，经 normalize_absolute 校验
    };

    /**
     *  路径判定结果。只表达"哪一条形态规则不满足"，不表达成功/失败之外的其他语义。
     */
    enum class PathVerdict : std::uint8_t
    {
        Ok                = 0,     // 通过校验与规范化
        NotAbsolute       = 1,     // 输入不是绝对路径
        NotNormalizable   = 2,     // 盘符不存在、重定向无法解析、权限不足、要求存在但不存在
        EscapesRoot       = 3,     // '..' 越出项目根
        UnsupportedForm   = 4,     // ADS ':'、尾部空格或点、保留设备名、非 POSIX 相对形态、空段
        TypeMismatch      = 5,     // 期望文件得到目录，或反之
        TooLong           = 6,     // 超平台限制
        ReparsePoint      = 7,     // 路径上任一段命中符号链接 / junction / 挂载点
    };

    /**
     *  一次路径校验的结果载体：verdict 为 Ok 时 text 有效，否则 detail 说明原因。
     *  纯数据载体不带判定逻辑，产出点一次填好两个字段。
     */
    struct PathCheck
    {
        PathVerdict verdict = PathVerdict::Ok;      // 判定结果
        std::string detail  = {};                   // 人读说明，可为空
        std::string text    = {};                   // 规范化结果的 UTF-8 文本（verdict == Ok 时有效）
    };

    /**
     *  @brief 规范化绝对路径：折叠分隔符、去 '.'、展开 '..'（不得越出根）、保留根名大小写
     *  @param text UTF-8 文本；必须以 '/' 分隔形式给出，盘符或 UNC 前缀原样保留
     *  @param must_exist 为真时要求路径存在，否则只做词法规范化（不访问磁盘）
     *  @return verdict 为 Ok 时 text 字段是 POSIX 分隔的规范化绝对路径；其它情况 detail 说明原因
     *  @note 只做词法与存在性检查，不判形态（保留名、ADS 由 normalize_relative 负责），也不跟随链接
     *  @note 绝对路径在根处截断 '..'，因此 EscapesRoot 不会由本函数返回
     */
    PathCheck normalize_absolute(std::string_view text, bool must_exist);

    /**
     *  @brief 校验并规范化项目内相对路径
     *  @param text POSIX 分隔的相对路径
     *  @return verdict 为 Ok 时 text 字段是规范化后的相对路径
     *  @note 拒绝：绝对路径、反斜杠、盘符、UNC、'..'、ADS ':'、尾部空格或点、Windows 保留设备名、
     *        空段；'.' 段按词法折叠掉
     *  @note 不做大小写改写：Windows 上大小写不同的同名路径由调用方按 last_segment_equals 之类的
     *        平台规则比较，本函数不修改输入的任何字节
     */
    PathCheck normalize_relative(std::string_view text);

    /**
     *  @brief 规范化的相对路径是否落在给定前缀之内
     *  @param relative 已通过 normalize_relative 的路径
     *  @param prefix 同口径的前缀，如 "IDPlugin"；空串表示项目根
     *  @return 按段比较命中前缀时返回 true
     *  @note 按段比较而不是字符串前缀比较："IDPluginX" 不在 "IDPlugin" 之内
     *  @note Windows 上按 ASCII 折叠比较（平台规则），其它平台按精确字节比较
     */
    bool is_under(std::string_view relative, std::string_view prefix) noexcept;

    /**
     *  @brief 最后一段是否等于给定名字
     *  @param relative 已规范化的相对路径
     *  @param name 段名，不含分隔符
     *  @return 相等返回 true；大小写按平台规则（Windows 折叠，其它平台精确）
     */
    bool last_segment_equals(std::string_view relative, std::string_view name) noexcept;

    /**
     *  @brief 取父目录的相对路径
     *  @param relative 已规范化的相对路径
     *  @return "a/b/c" -> "a/b"；"a" -> ""（项目根）
     */
    std::string_view parent_relative(std::string_view relative) noexcept;

    /**
     *  @brief 取最后一段
     *  @param relative 已规范化的相对路径
     *  @return "a/b/c" -> "c"；空串输入返回空串
     */
    std::string_view file_name(std::string_view relative) noexcept;

    /**
     *  @brief 逐段检查重解析点（符号链接、junction、挂载点）
     *  @param absolute 已规范化的绝对路径；路径可以不存在（只检查到不存在的第一段为止）
     *  @return 命中时 verdict 为 ReparsePoint、detail 指出命中的路径段；无法读取属性时为 NotNormalizable
     *  @note 只读；不跟随链接，按 lstat 语义判定（Windows 用不含跟随标志的属性查询）
     *  @note 不做结果缓存：计划与写入之间路径可能被替换，调用方在写入与删除前各调用一次
     */
    PathCheck check_reparse_points(const std::filesystem::path& absolute);

    /**
     *  @brief 逐段检查重解析点，只判 `root` **之内**的段
     *  @param absolute 已规范化的绝对路径，应落在 `root` 之内
     *  @param root 判定下界（项目根、工程根）；自身与更上层不判，它们属于调用方环境
     *  @return 同 `check_reparse_points`；`absolute` 不在 `root` 之内时为 UnsupportedForm
     *  @note 用途：写入与裁剪目标的安全性只看项目根之内有没有链接，不把"项目根挂在一个链路上"
     *        误判成越界；工程根的源资产侧同理（spec §2.3）
     */
    PathCheck check_reparse_points_under(const std::filesystem::path& absolute,
                                         const std::filesystem::path& root);

    /**
     *  @brief 把绝对路径里**已存在前缀**展开为长名形态（Windows 8.3 短名展开），不存在的后缀原样保留
     *  @param text 已按 normalize_absolute 规范化的 POSIX 分隔绝对路径
     *  @return Windows 上为长名形态；路径整体不存在时只展开最深的已存在前缀；其它平台原样返回
     *  @note 只读：只做属性查询与 `GetLongPathNameW`，不创建、不改名、不写盘
     *  @note 用于"同一目录因 8.3 名被登记两次"（spec §2.1）：比较与登记写入前先把两侧统一到长名形态
     */
    std::string expand_existing_prefixes(std::string_view text);

    /**
     *  @brief 两条绝对路径文本是否指向同一路径（平台规则 + 8.3 短名展开）
     *  @param left 规范化后的 POSIX 分隔绝对路径
     *  @param right 同口径的绝对路径
     *  @return 视为同一路径返回 true
     *  @note 先按平台规则比较文本（命中即相等，不访问磁盘）；不相等时在 Windows 上把两侧展开为长名形态再比较。
     *        因此同一目录的短名/别名、不同分隔符与大小写差异都判为相等（spec §2.1）
     *  @note 其它平台没有 8.3 别名：按精确字节比较
     */
    bool is_same_absolute_path(std::string_view left, std::string_view right);

    /**
     *  @brief UTF-8 文本转原生路径
     *  @param utf8 UTF-8 文本
     *  @return 原生路径；Windows 下经 u8string 转换，不经 ANSI 代码页
     */
    std::filesystem::path to_native(std::string_view utf8);

    /**
     *  @brief 原生路径转 UTF-8 文本
     *  @param path 原生路径
     *  @return UTF-8 文本，分隔符按平台原生形式（Windows 为 '\\'）
     */
    std::string to_utf8(const std::filesystem::path& path);

    /**
     *  @brief 绝对路径转项目内相对路径
     *  @param absolute 绝对路径，应已按 normalize_absolute 规范化
     *  @param project_root 项目根绝对路径
     *  @return 落在项目根内时返回 POSIX 分隔的相对路径（等于项目根时为空串）；否则返回 std::nullopt
     *  @note 只做词法转换，不判断"是否在 IDPlugin 内"，也不访问磁盘
     *  @note Windows 上词法比较区分大小写，调用方应传入同源规范化结果
     */
    std::optional<std::string> to_relative(const std::filesystem::path& absolute,
                                           const std::filesystem::path& project_root);

    /**
     *  @brief 平台路径长度判定
     *  @param absolute 绝对路径
     *  @return Windows 上按扩展长度上限（32767 个 UTF-16 单元，计入 \\?\ 前缀）不超限时返回 true；
     *          其它平台不判长度，恒返回 true
     *  @note 超长时不再尝试短路径名回退：静默截断或依赖 8.3 短名都属于误报，据实返回 false
     */
    bool is_path_length_ok(const std::filesystem::path& absolute) noexcept;
} // namespace ID::Project
