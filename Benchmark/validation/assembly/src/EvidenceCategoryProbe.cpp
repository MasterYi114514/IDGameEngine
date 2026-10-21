#include <cstddef>
#include <cstdint>

#include "IDAssembly.generated.hpp"

/**
 *  构建证据反向用例探针（P2）
 *  1. 本 TU 只 include 组装层在**当前构建目录**生成的 IDAssembly.generated.hpp：
 *     驱动器会把它复制到某个被改坏证据的目录里编译，因此编译的就是"被改坏的那份生成输入"；
 *  2. 期望的错误类别与阶段由 -DPROBE_EXPECT_CODE / -DPROBE_EXPECT_PHASE 给出，
 *     判定是 static_assert 的枚举相等比较：类别不对就编译失败，不存在"看起来跑过了"的可能；
 *  3. 缺少期望宏时直接 #error：不允许在没写期望的情况下编译通过；
 *  4. 合法对照用 PROBE_EXPECT_CODE=None / PROBE_EXPECT_PHASE=None，同一个 TU 同一份环境。
 */

#ifndef PROBE_EXPECT_CODE
    #error "EvidenceCategoryProbe requires -DPROBE_EXPECT_CODE=<PluginError enumerator>"
#endif
#ifndef PROBE_EXPECT_PHASE
    #error "EvidenceCategoryProbe requires -DPROBE_EXPECT_PHASE=<AssemblyPhase enumerator>"
#endif

namespace
{
    using ID::Assembly::Generated::kPlan;
    using ID::PluginCompileTime::AssemblyPhase;
    using ID::PluginCompileTime::PluginError;

    /**
     *  @brief 计划诊断的错误类别是否等于驱动器声明的期望值
     *  @return 相等返回 true
     */
    constexpr bool probe_code_matches() noexcept
    {
        return kPlan.diagnostic.code == PluginError::PROBE_EXPECT_CODE;
    }

    /**
     *  @brief 计划诊断的失败阶段是否等于驱动器声明的期望值
     *  @return 相等返回 true
     */
    constexpr bool probe_phase_matches() noexcept
    {
        return kPlan.diagnostic.phase == AssemblyPhase::PROBE_EXPECT_PHASE;
    }

    /**
     *  @brief 诊断自洽：成功必须同时是 code==None 与 phase==None，失败必须两者都不是 None
     *  @return 自洽返回 true
     */
    constexpr bool probe_rejection_is_consistent() noexcept
    {
        const bool code_ok  = (kPlan.diagnostic.code == PluginError::None);
        const bool phase_ok = (kPlan.diagnostic.phase == AssemblyPhase::None);
        return code_ok == phase_ok;
    }

    static_assert(probe_code_matches(),
                  "corrupted-evidence probe: plan diagnostic code != expected category");
    static_assert(probe_phase_matches(),
                  "corrupted-evidence probe: plan diagnostic phase != expected phase");
    static_assert(probe_rejection_is_consistent(),
                  "corrupted-evidence probe: code and phase disagree on success/failure");
} // namespace
