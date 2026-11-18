#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 *  生成前校验与写入期失败的诊断取值类型：诊断类别、单条诊断与校验报告集合。
 *
 *  本头只持取值，不做判定：类别由 validate(const Project&) 与组装阶段的参数校验产出，
 *  写入期失败由 FragmentWriter 装进 FragmentOutcome。校验逻辑全部落在 Project/ 与 File/，
 *  因此本头不 include 也不前向声明 Project / Directory / Target，避免 Common/ 反向依赖工程模型。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  诊断类别：只回答「是什么」，不回答「成不成」，因此没有表成功的取值，也没有哨兵取值。
     *  成功与失败由 ValidationReport::is_valid 与 FragmentOutcome 的 diagnostic 空态表达（cpp-style 第 9 节）。
     *  取值按诊断表顺序连续编号；类别名与 to_string 的返回值一致，CLI 用后者输出 failed:<类别>。
     */
    enum class CMakeError : std::uint8_t
    {
        EmptyName                  = 0,  // project()、目标名或子目录名为空串
        InvalidName                = 1,  // 名称含 CMake 标识符不允许的字符，或首字符不是字母
        InvalidBareArgument        = 2,  // 裸值为空或含字符集外字符；注释头取值含 CR/LF 或控制字符
        InvalidTargetType          = 3,  // 生成前目标类型仍是 TargetType::None
        UnsupportedCompileStandard = 4,  // set_compile_standard 收到 cxx_std_20 以外的取值
        InvalidSourceGlobDir       = 5,  // 收源目录不是 src、含 .. 段、绝对路径或带盘符
        DuplicateTarget            = 6,  // 目标名在另一目录重复；同目录重名是该判定的子集
        DuplicateSubdirectory      = 7,  // 同一目录内子目录名重复
        DuplicateDerivedVariable   = 8,  // 目标派生名与变量名或其它派生名按大写折叠相同
        MissingSourceGlobDir       = 9,  // 非 InterfaceLibrary 目标未设置收源目录
        InterfaceTargetScope       = 10, // InterfaceLibrary 的条目使用 Public 或 Private
        InterfaceTargetSetting     = 11, // InterfaceLibrary 调用了输出目录、编译特性或收源设置
        InvalidFormOptionTarget    = 12, // 形态选项用于静态/共享库以外的类型，或未引用同目录 Option
        MissingImportLocation      = 13, // imported 可执行/库目标未提供 location
        ImportTargetSetting        = 14, // imported 目标调用了不适用的设置项或提供了不适用的产物
        ImportTargetScope          = 15, // imported 目标的条目使用 Public 或 Private
        VerbatimArgument           = 16, // Verbatim 出现在原始词法片段之外的参数值位置
        LinkTargetNotFound         = 17, // 链接库名既非本工程目标也不在外部目标登记集合内
        PathEscapesProjectRoot     = 18, // 写入路径解析后不在项目根之内
        TargetModified             = 19, // 受控片段替换前重读目标，内容与先前读取不一致
        MissingFile                = 20, // 受控片段写入的目标不存在
        MissingMarker              = 21, // 目标缺 begin 或 end 标记，或标记不独占一行
        AmbiguousMarker            = 22, // 标记重复，或 begin 出现在 end 之后
        IoFailed                   = 23, // 打开、读取、创建临时文件、替换、建父目录或删除失败
        InvalidConfigOutput        = 24, // 按配置输出目录为空、只给一半，或与单目录表达同时设置
        InvalidMacroBranch         = 25, // 分支条件为空、两分支载荷类型不同，或变量载荷取值为空
        InvalidGuard               = 26, // 守卫目标名为空或不是 Bare，或说明串含 CR/LF
        DuplicateFragment          = 27, // 多片段写入里重复片段名，或片段文本范围重叠
        InvalidBuildRpath          = 28, // BUILD_RPATH 为空列表、含空串项或含 $<CONFIG> 的取值
    };

    // 类别取值是诊断契约的一部分：新增类别时同步扩大该断言与 to_string 的分支
    static_assert(static_cast<std::uint8_t>(CMakeError::InvalidBuildRpath) == 28,
                  "CMakeError 必须连续覆盖 29 个诊断类别");

    /**
     *  @brief 取得诊断类别的标识符文本
     *  @param error 诊断类别
     *  @return 与枚举名一致的类别名，如 "MissingImportLocation"；返回值指向静态存储，生命周期覆盖整个进程
     *  @note 只用于人读输出与 CLI 的 failed:<类别>；不要用它判断成败，成败看 is_valid 与 diagnostic 空态
     */
    std::string_view to_string(CMakeError error) noexcept;

    /**
     *  单条诊断：类别、对象、期望、实际、定位与下一步动作（id-kernel 第 10 节、foundation.md 第 7 节）。
     *  数据载体不带判定，只在产出点填值；下一步动作必须可执行（补哪个字段、改哪个取值、重跑哪条命令）。
     *  CMakeError 没有表成功的取值可当默认值，因此 error 不设默认成员初始化器：产出点用聚合初始化一次给全六项，
     *  只在确实要填值时才构造本对象。
     */
    struct Diagnostic
    {
        CMakeError   error;          // 诊断类别
        std::string  subject;        // 涉及对象：目标名、路径或命令
        std::string  expected;       // 期望
        std::string  actual;         // 实际
        std::string  location;       // 目录相对路径与定位序号
        std::string  next_action;    // 下一步可执行动作
    };

    /**
     *  校验报告集合：is_valid 是成功标志，diagnostics 按产出顺序排列。
     *  纯数据载体不带逻辑（cpp-style 第 12 节）：追加与置位由产出点完成——diagnostics.push_back(...)
     *  之后把 is_valid 置 false。一旦出现过诊断，is_valid 不会自动恢复；同一轮校验要重跑时用新的报告对象。
     */
    struct ValidationReport
    {
        bool                    is_valid    = true;     // 有任何一条诊断即置 false
        std::vector<Diagnostic> diagnostics = {};       // 追加顺序即判定顺序
    };
} // namespace ID::CMake
