#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cmake/Common/Diagnostic.hpp"
#include "cmake/File/FragmentLayout.hpp"

/**
 *  落盘层：整文件写入与受控片段替换，含幂等、原子替换、路径越界拒绝与六类写入期诊断。
 *
 *  职责边界：只回答「给定内容落到项目根内的哪个文件、怎么落」，不生成任何 CMake 语义、不做依赖与清单判定、
 *  不决定产出范围。写入方式与 EmitScope 的映射由调用方负责：WholeFile 只允许 write_whole_file，
 *  BodyOnly 只允许 write_managed_block，两路不得互换（CMake 封装计划书 §5.6、§5.8 第 8 条）。
 *
 *  写入协议（计划书 §5.8 第 1–8 条）：先算后写；同目录临时文件 + 改名替换；替换前重读目标确认未被改动；
 *  内容相同则幂等跳过；失败只清理本进程创建的临时文件，退出后不留锁文件或占位文件；文本统一 UTF-8 写出，
 *  不把 BOM 写进生成内容。
 *
 *  受控片段边界（计划书 §5.8 受控片段规则 1–6）：begin 与 end 各独占一行且按行首精确匹配（行尾允许 CRLF/LF），
 *  恰好各一条且 begin 在 end 之前；只替换两标记之间的字节，标记外字节逐字保留（含既有 BOM 与既有行尾）；
 *  片段内容按文件第一个换行符决定的既有行尾写出，禁止整文件行尾归一化。
 *
 *  并发：不承诺跨进程或跨线程并发安全，调用方串行调用；项目级锁由工具阶段层承担（references/memory.md 第 8 节）。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  一次写入的结果：只回答「有没有写」与「写了多少」，成败由 diagnostic 的空态配合 is_success 表达。
     *  数据载体不带判定（cpp-style 第 12 节）：幂等跳过是成功但未写入，失败可能发生在任何落盘动作之前。
     */
    struct FragmentOutcome
    {
        bool                      is_written = false;   // 本次调用是否发生了写入
        std::size_t               bytes      = 0;       // 本次写入内容的字节数；幂等跳过与失败为 0
        std::optional<Diagnostic> diagnostic = {};      // 失败原因与定位；成功与幂等跳过均为空
    };

    /**
     *  @brief 判定一次写入是否成功：写入成功与幂等跳过都为 true，失败为 false
     *  @param outcome 写入结果
     *  @return diagnostic 为空即成功；不把 is_written 当成成败依据
     *  @note 成功且 is_written 为 false 表示幂等跳过（内容已在目标中，本次没有重写）
     */
    bool is_success(const FragmentOutcome& outcome) noexcept;

    /**
     *  多片段写入的一个片段：片段名与要写出的新内容（行尾 '\n'，写出时按目标文件既有行尾展开）。
     */
    struct ManagedBlock
    {
        std::string name    = { };   // 片段名，必须出现在文件标记里
        std::string content = { };   // 新片段内容，行尾 '\n'
    };

    /**
     *  一条片段在本次多片段写入中的结果：只回答「这个片段有没有写、写出范围是什么」，
     *  不含任何整文件语义（整文件是否替换由全部片段结果的合取决定，不由调用方逐条推断）。
     */
    struct ManagedWriteResult
    {
        std::string               fragment     = { };   // 片段名
        bool                      is_written   = false; // 该片段本次是否真的写入新字节；幂等跳过为 false
        std::string               range_digest = { };   // 写出后该片段文本范围的摘要；未写入时为空
        std::optional<Diagnostic> diagnostic   = { };   // 该片段未写入的原因；成功与幂等跳过均为空
    };

    /**
     *  只读快照：整文件字节 + 指定片段的原始字节与摘要（供装配阶段算片段 before 摘要与组合目标全文）。
     */
    struct FragmentSnapshot
    {
        FragmentState              state       = FragmentState::Missing;   // 解析结论
        std::string                file_bytes  = { };                      // 读到时的整文件原始字节
        std::string                file_digest = { };                      // 整文件字节摘要；未读到字节时为空
        std::vector<FragmentRange> ranges      = { };                      // 按 offset 升序；仅 state 为 Ok 时有意义
        std::string                detail      = { };                      // 非 Ok 时的说明文本
        std::optional<Diagnostic>  diagnostic  = { };                      // 读取失败的原因；成功与标记异常均为空
    };

    /**
     *  把文本落到项目根内的文件：整文件写入与受控片段替换两个入口。
     *
     *  路径：入参是相对项目根的路径（POSIX 或平台分隔符均可），也接受解析后仍落在项目根内的绝对路径
     *  （盘符或根名大小写不同不算越界）；`..` 逃逸、项目外绝对路径、指向项目外的符号链接一律拒绝。
     *  超长路径与保留设备名在 Windows 上立即失败、不静默截断；其它平台没有这两项限制，不判。
     *
     *  诊断：PathEscapesProjectRoot、TargetModified、MissingFile、MissingMarker、AmbiguousMarker、IoFailed
     *  六类装入 FragmentOutcome::diagnostic，六字段齐全且 next_action 是可执行动作。
     */
    class FragmentWriter
    {
    public:
        /**
         *  @brief 绑定一个项目根
         *  @param project_root 项目根的 UTF-8 原生路径；构造后只读，全部写入都解析到该根之内
         */
        explicit FragmentWriter(std::string project_root);

        FragmentWriter(const FragmentWriter&)            = delete;
        FragmentWriter& operator=(const FragmentWriter&) = delete;

        /**
         *  @brief 整文件写入：创建或整体替换工具自有生成物，内容按 UTF-8 原样落盘（不做行尾转换、不写 BOM）
         *  @param relative_path 相对项目根的文件路径，如 "IDGenerated/CMakeLists.txt"
         *  @param content 完整文件内容；父目录按需创建
         *  @return 写入成功为 is_written=true、bytes=content.size()；内容与现有文件完全相同为幂等跳过
         *  @note 只用于可重建的工具自有生成物，不参与资产冲突检测；
         *        替换前会重读目标并与本次读取的快照比较，不一致返回 TargetModified 且不替换
         */
        FragmentOutcome write_whole_file(const std::string& relative_path, std::string_view content);

        /**
         *  @brief 受控片段替换：只替换 begin/end 两标记行之间的内容，标记外字节逐字保留
         *  @param relative_path 相对项目根的目标文件路径；文件必须已存在，本函数不创建文件
         *  @param content 片段内容，行尾用 '\n'；写出时按文件既有行尾展开
         *  @param begin_marker begin 标记整行文本（不含行尾），必须独占一行
         *  @param end_marker end 标记整行文本（不含行尾），必须独占一行
         *  @return 替换成功为 is_written=true、bytes=写出的新文件字节数；片段未变化为幂等跳过
         *  @note 目标不存在报 MissingFile；标记缺失或不独占一行报 MissingMarker；
         *        标记重复或 begin 在 end 之后报 AmbiguousMarker；三种情形都不写入、不追加、不创建父目录。
         *        空标记或含 CR/LF 的标记属调用方违约，用 ID_ASSERT 终止（标记由组装阶段提供）。
         */
        FragmentOutcome write_managed_block(const std::string& relative_path,
                                            std::string_view content,
                                            std::string_view begin_marker,
                                            std::string_view end_marker);

        /**
         *  @brief 一次替换同文件内的多个受控片段，只做一次原子替换
         *  @param relative_path 目标文件（必须已存在）；解析规则与其它入口一致，受项目根范围约束
         *  @param blocks 片段列表；顺序无语义，按文件中的实际位置组合，同名片段属调用方违约
         *  @return 逐片段结果，顺序与 blocks 一致；整次调用是全成或全不成——任一片段失败时所有元素
         *          is_written 为 false、range_digest 为空，diagnostic 给出同一条失败原因
         *  @note 失败类别：标记缺失或不成对 → MissingMarker；标记重复或 begin 在 end 之后 → AmbiguousMarker；
         *        重复片段名或片段范围重叠 → DuplicateFragment；目标不存在 → MissingFile。
         *        新内容若让写后文件无法按同一套标记规则解析（例如内容里出现同名标记行），按同一映射报错。
         *        以上都不写任何内容，片段外字节逐字保留
         *  @note 替换前重读目标并与本次读取的快照比较，不一致返回 TargetModified（与单片段入口同一行为）
         *  @note 若某片段新内容与现状相同，则该片段 is_written 为 false、range_digest 为空，
         *        但整文件仍只在有变化时替换
         *  @note 片段范围摘要只能经 fragment_range_digest 计算：本函数不自行拼接片段文本再哈希，
         *        也不接收装配层的摘要类型（ID::CMake 不认识跨层类型）
         */
        std::vector<ManagedWriteResult> write_managed_blocks(const std::string& relative_path,
                                                             std::span<const ManagedBlock> blocks);

        /**
         *  @brief 只读快照：整文件字节 + 指定片段的原始字节与摘要
         *  @param relative_path 目标文件；解析规则与其它入口一致
         *  @param names 需要定位的片段名
         *  @return 文件不存在时 state 为 Missing；标记异常时给出对应状态与 detail；读取失败时 diagnostic 非空
         *  @note 只读：不创建文件、不建目录、不改动任何字节；file_digest 是对整文件字节调用
         *        fragment_range_digest 得到的十六进制文本
         */
        FragmentSnapshot read_fragments(const std::string& relative_path, std::span<const std::string_view> names);

    private:
        std::string m_project_root;   // 项目根的 UTF-8 原生路径，构造后只读
    };
} // namespace ID::CMake
