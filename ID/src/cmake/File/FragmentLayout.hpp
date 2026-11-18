#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 *  受控片段的标记解析与组合：两个片段（output-dirs / plugin-targets）的定位、文本范围切分、
 *  片段正文替换、首次创建时的整文件组装，以及片段文本范围的摘要。
 *
 *  职责边界：只处理「标记 → 范围 → 文本」这一层结构，不读文件、不写文件、不做冲突判定与事务
 *  （落盘由 File/FragmentWriter 负责，冲突三态由装配阶段按摘要判定），也不认识任何项目语义。
 *
 *  不变量：
 *  - 片段文本范围 = BEGIN 标记行行首 → END 标记行行尾（含其后的换行符），按文件原始字节切出，
 *    不重新换行、不做编码转换；两行标记本身属于该范围，是其摘要对象的一部分。
 *  - 标记按行首精确匹配（整行等于「前缀 + 片段名」），行尾允许 CRLF/LF；一个片段恰好一条 BEGIN
 *    与一条 END，且 BEGIN 在 END 之前；违反即对应的 FragmentState，不做自动修复。
 *  - 本文件是标记解析与组合的唯一实现：多片段写入与装配阶段都经本文件，不另写一套。
 *  - 片段摘要只经 fragment_range_digest 计算并返回小写十六进制文本：ID::CMake 不认识装配层的
 *    摘要类型，也不得在别处拼出片段文本再哈希。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    // 片段名与标记前缀：标记行形如 "# >>> ID GENERATED BEGIN output-dirs"，片段名固定为这两个取值
    inline constexpr std::string_view kFragmentOutputDirs    = "output-dirs";
    inline constexpr std::string_view kFragmentPluginTargets = "plugin-targets";
    inline constexpr std::string_view kFragmentBeginPrefix   = "# >>> ID GENERATED BEGIN ";
    inline constexpr std::string_view kFragmentEndPrefix     = "# <<< ID GENERATED END ";

    /**
     *  一个片段在文件字节里的位置：begin 标记行行首 → end 标记行行尾（含其后的换行符）。
     *  offset/length 以字节计，不含重新换行与编码转换；bytes 是该范围的原始字节（含两行标记）。
     */
    struct FragmentRange
    {
        std::string  name   = { };   // 片段名
        std::size_t  offset = 0;     // 范围起点（字节偏移）
        std::size_t  length = 0;     // 范围长度（字节）
        std::string  bytes  = { };   // 该范围的原始字节（含两行标记）
    };

    /**
     *  解析结论。缺成功标志以外的取值只回答「哪里不对」，不回答「怎么修」；具体修复动作由调用方给。
     */
    enum class FragmentState : std::uint8_t
    {
        Ok         = 0,   // 全部片段各一组标记、顺序正确且范围不重叠
        Missing    = 1,   // 某个片段一条标记都没有（整文件不存在时也算）
        Incomplete = 2,   // begin 或 end 缺一半
        Ambiguous  = 3,   // 标记重复，或 begin 在 end 之后
        OutOfOrder = 4,   // 两个片段的范围重叠
    };

    static_assert(sizeof(FragmentState) == sizeof(std::uint8_t),
                  "FragmentState 的底层类型必须与声明一致，否则按值传递与诊断取值约定失效");

    /**
     *  一次解析的结果：结论、说明与按 offset 升序排列的片段范围。数据载体不带判定。
     */
    struct FragmentLayout
    {
        FragmentState              state  = FragmentState::Ok;   // 解析结论
        std::string                detail = { };                 // 非 Ok 时的说明文本
        std::vector<FragmentRange> ranges = { };                 // 按 offset 升序；仅 Ok 时有意义
    };

    /**
     *  @brief 解析给定文件字节里指定片段的标记
     *  @param file_bytes 整文件原始字节
     *  @param names 需要查找的片段名（本流程固定两个）；重复给出同名片段会因范围重叠判 OutOfOrder
     *  @return 解析结论与片段范围；非 Ok 时 ranges 留空，detail 说明第一个出问题的片段
     *  @note 标记按行首精确匹配，行尾允许 CRLF/LF；一个片段恰好一条 begin 与一条 end，且 begin 在 end 之前；
     *        违反即对应 FragmentState，不做自动修复。判定按 names 顺序进行，报告第一个出问题的片段
     */
    FragmentLayout parse_fragments(std::string_view file_bytes, std::span<const std::string_view> names);

    /**
     *  @brief 用新片段内容组合目标全文：片段范围内替换、片段外字节逐字保留
     *  @param file_bytes 当前整文件字节
     *  @param layout 已解析的布局（state 必须为 Ok，且 ranges 必须来自同一份 file_bytes）
     *  @param replacements 每个片段的新内容（不含标记行），片段名按文本比较
     *  @return 组合后的整文件文本；layout 非 Ok、缺少某片段的新内容或范围越出 file_bytes 时返回空 optional
     *  @note 标记行本身保持不变（不重写、不升级）；片段内容的行尾按文件第一个换行符决定；
     *        内容里的 CRLF 按一次换行处理，避免写出 CRCRLF
     */
    std::optional<std::string> compose_fragments(
        std::string_view file_bytes,
        const FragmentLayout& layout,
        std::span<const std::pair<std::string_view, std::string_view>> replacements);

    /**
     *  @brief 组装一份「只有两个片段」的完整文件（首次创建用）
     *  @param header 第 1–2 项文本（`EmitScope::HeaderOnly` 产出）
     *  @param output_dirs_body 片段体（`EmitScope::ConfigurationOnly` 产出）
     *  @param plugin_targets_body 片段体（`EmitScope::TargetsOnly` 产出）
     *  @return 完整文件文本，顺序为 header → output-dirs 片段 → plugin-targets 片段；
     *          段与段之间一个空行（该空行在片段范围之外，之后的片段替换不动它），行尾固定 '\n'
     *  @note 片段体按 '\n' 写出并在结尾补一个换行；空片段的 BEGIN 与 END 相邻，不额外加空行
     */
    std::string compose_new_root_file(std::string_view header,
                                      std::string_view output_dirs_body,
                                      std::string_view plugin_targets_body);

    /**
     *  @brief 计算片段文本范围的摘要（片段 N 的唯一计算入口）
     *  @param file_bytes 整文件原始字节；range 必须来自同一份字节的 parse_fragments / read_fragments 结果
     *  @param range 片段范围：offset/length 以字节计，含两行标记
     *  @return 小写十六进制摘要文本（SHA-256，64 个字符）；range 为空、偏移越界或长度越出 file_bytes 时返回空串
     *  @note 摘要对象是「BEGIN 标记行行首 → END 标记行行尾（含其后换行）」的原始字节，不重新换行、
     *        不做编码转换；算法取 SHA-256 是因为装配层用它记录 manifest 条目摘要（项目流程 spec §1.3、§6.2），
     *        本层不引入外部依赖，也不把整文件读入后再拼接片段文本
     */
    std::string fragment_range_digest(std::string_view file_bytes, const FragmentRange& range);
} // namespace ID::CMake
