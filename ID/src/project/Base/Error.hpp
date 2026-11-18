#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 *  项目流程的失败类别、退出码取值与全序优先级。
 *
 *  本头只持有取值与映射，不做业务判定：谁算冲突、哪份记录可信、哪些路径该删都在各自单元，
 *  本头只回答"这个类别映到哪个退出码、在全序里排第几"。
 *
 *  不变量：
 *  1. 一个类别只映一个退出码，exit_mapping 是唯一映射表；调用点不得写死退出码数字。
 *  2. 类别取值与 spec §13.2 的诊断类别表逐条对应（38 类），to_identifier 返回 spec 的 snake_case
 *     稳定标识（脚本按它断言），枚举名与标识各改一头即破坏脚本契约。
 *  3. `check_diff` 不是类别：计划只有变更、没有冲突时的差异码 6 由 Diagnostic.hpp 的 has_plan_diff
 *     表达，因此本枚举没有该取值。
 *  4. warn 类别（registry_marker_stale、plugin_config_invalid、dependency_unknown）没有退出映射，
 *     不写成"码 0"，由 exit_mapping 的 is_mapped 为假表示。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  诊断类别：只回答「是什么」，不回答「成不成」。成败走 bool / 空指针 / std::optional 通道，
     *  组合状态由 DiagnosticSet 承载，本枚举不承载成功取值。
     *  取值按 spec §13.2 的表序连续编号；新增类别必须先改 spec §13.2，再改本枚举与下面的 static_assert。
     */
    enum class ProjectCategory : std::uint8_t
    {
        // 退出码 3：用法
        CliUsage                       = 0,
        SelectionSourceConflict        = 1,
        // 退出码 1：输入或一致性
        RegistryInvalid                = 2,
        ProjectExists                  = 3,
        ProjectNotFound                = 4,
        ProjectNameInvalid             = 5,
        ProjectPathInvalid             = 6,
        ProjectPathOverlap             = 7,
        ProjectNotInitialized          = 8,
        ProjectConfigMissing           = 9,
        ProjectConfigInvalid           = 10,
        ProjectNameMismatch            = 11,
        PluginDuplicate                = 12,
        PluginUnknown                  = 13,
        DependencyCycle                = 14,
        RequirementConflict            = 15,
        OptionUnknown                  = 16,
        OptionValueInvalid             = 17,
        AssetMissing                   = 18,
        PluginAssetIncomplete          = 19,
        PathOutsideProject             = 20,
        CmakeNameConflict              = 21,
        StaleGenerated                 = 22,
        // 退出码 2：写入、所有权、记录、恢复、并发
        WriterConflict                 = 23,
        UnmanagedContent               = 24,
        ChangedSincePlan               = 25,
        WriteFailed                    = 26,
        RecoveryConflict               = 27,
        RecordInvalid                  = 28,
        SnapshotUnstable               = 29,
        // 退出码 4：构建
        BuildEnvironmentUnsupported    = 30,
        BuildInvokeFailed              = 31,
        BuildFailed                    = 32,
        ArtifactMissing                = 33,
        // 退出码 5：清理阻断
        CleanupBlocked                 = 34,
        // 无退出映射（warn）
        RegistryMarkerStale            = 35,
        PluginConfigInvalid            = 36,
        DependencyUnknown              = 37,
    };

    // 类别数量是诊断契约的一部分：新增或删除类别时同步改 spec §13.2、本断言与 to_identifier 的分支
    static_assert(static_cast<std::uint8_t>(ProjectCategory::DependencyUnknown) == 37,
                  "ProjectCategory 必须连续覆盖 spec §13.2 的 38 个类别：35 个有退出映射，"
                  "RegistryMarkerStale/PluginConfigInvalid/DependencyUnknown 三类 warn 无退出映射"
                  "（check_diff 不是类别）");

    // spec §13.1 的退出码取值：数字只在本头与 exit_mapping 出现，调用点不写死
    inline constexpr int kExitSuccess        = 0;   // 成功；--check 无差异
    inline constexpr int kExitValidation     = 1;   // 输入或一致性校验失败
    inline constexpr int kExitOwnership      = 2;   // 写入、所有权、记录、恢复或并发相关失败
    inline constexpr int kExitUsage          = 3;   // 用法错误
    inline constexpr int kExitBuild          = 4;   // 构建环境或构建失败
    inline constexpr int kExitCleanupBlocked = 5;   // 清理被阻塞
    inline constexpr int kExitCheckDiff      = 6;   // 只由 has_plan_diff 决定，不来自任何类别

    /**
     *  @brief 取得类别的稳定标识符
     *  @param category 诊断类别
     *  @return spec §13.2 的 snake_case 类别名，如 "unmanaged_content"；返回值指向静态存储，
     *          进程生命周期有效
     *  @note 只用于人读输出与脚本断言；不要用它判断成败，成败看诊断集合与退出码
     */
    std::string_view to_identifier(ProjectCategory category) noexcept;

    /**
     *  @brief 取得类别的一句话中文说明
     *  @param category 诊断类别
     *  @return 人读说明，用于终端输出；返回值指向静态存储，进程生命周期有效
     *  @note 说明只解释"这一类别是什么意思"，不代替诊断的五要素（对象、期望、实际、下一步）
     */
    std::string_view to_text(ProjectCategory category) noexcept;

    /**
     *  类别到退出码的映射结果。is_mapped 为假表示 warn 类别，不参与退出码选择。
     */
    struct ExitMapping
    {
        int     code        = kExitSuccess;     // 退出码取值，见上面的 kExit* 常量
        bool    is_mapped   = false;            // 是否有退出映射；false 为 warn 类别
    };

    /**
     *  @brief 取类别的退出码映射
     *  @param category 诊断类别
     *  @return 该类别唯一的退出码与"是否参与退出码选择"；warn 类别返回 is_mapped 为假
     *  @note 本函数是唯一映射表；新增类别只在此处登记一次，调用点不复制 switch
     */
    ExitMapping exit_mapping(ProjectCategory category) noexcept;

    // 不参与比较的优先级取值：warn 类别与域外类别取此值，比较时一律排在最后
    inline constexpr std::uint8_t kNoPriority = 0xFF;

    /**
     *  @brief 取类别在 spec §13.1 全序里的优先级序号
     *  @param category 诊断类别
     *  @return 0–5 的优先级序号，数值越小越优先：3(0) > 1(1) > 2(2) > 5(3) > 4(4) > 0(5)；
     *          warn 类别返回 kNoPriority（不参与比较）
     *  @note 序号按"用法错误 3 > 输入校验失败 1 > 记录/所有权/并发 2 > 清理被阻断 5 >
     *        构建环境或构建失败 4 > 计划差异 6 > 成功 0"的顺序给出，与退出码数值大小无关
     */
    std::uint8_t exit_priority(ProjectCategory category) noexcept;
} // namespace ID::Project
