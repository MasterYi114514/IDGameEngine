#include <cstring>

#include "benchmark_support.hpp"
#include "RuntimeCases.hpp"
#include "RuntimeFixture.hpp"

/**
 *  运行期 PluginRuntime 校验入口
 *  1. 输出全部走 ID::Bench 公共设施：console 与本次的 out/report.txt 由同一个 tee 产生，
 *     逐字一致；官方 Benchmark/report.txt 只由 Benchmark 目标覆盖写，本程序不碰它；
 *  2. 只有一个正确性域，没有性能段：不做性能测量、不新增评级、不引用任何参考值；
 *  3. 退出码由 ExitCode() 给出：任一 Check 失败即非 0，驱动器据此判定；
 *  4. 契约上可能 abort 的单条路径用命令行参数隔离成一次独立进程运行，
 *     这样一次 abort 不会抹掉主矩阵的全部结果（methodology.md 第 4 节的子进程隔离）。
 */

namespace
{
    constexpr char kZeroNullProbeCase[]        = "--case-zero-null-shutdown";
    constexpr char kZeroNullRecordsProbeCase[] = "--case-zero-null-records-shutdown";

    int run_single_case(ID::Bench::TestFn case_fn) noexcept
    {
        ID::Bench::BeginSession();
        ID::Bench::Run("ID.PluginRuntime", case_fn, nullptr);
        ID::Bench::EndSession();
        return ID::Bench::ExitCode();
    }
} // namespace

namespace ID::Bench
{
    void IDPluginRuntimeSecurityTest() noexcept
    {
        // 前置条件：进入本域时 runtime 必须是 Idle 且 64 个槽全空
        Runtime::check_state("domain.precondition_idle", PluginRuntimeState::Idle);
        Runtime::check_slots_empty("domain.precondition_slots_empty");

        Runtime::run_lifecycle_cases();
        Runtime::run_service_cases();
        Runtime::run_plan_cases();
        Runtime::run_provision_cases();

        // 后置条件：全部用例结束后不得留下活动计划、活动 owner 或残留槽
        Runtime::check_state("domain.postcondition_idle", PluginRuntimeState::Idle);
        Runtime::check_slots_empty("domain.postcondition_slots_empty");
    }
} // namespace ID::Bench

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        if (std::strcmp(argv[1], kZeroNullProbeCase) == 0)
        {
            return run_single_case(&ID::Bench::Runtime::ZeroNullShutdownProbe);
        }
        if (std::strcmp(argv[1], kZeroNullRecordsProbeCase) == 0)
        {
            return run_single_case(&ID::Bench::Runtime::ZeroNullRecordsShutdownProbe);
        }
    }

    ID::Bench::BeginSession();

    ID::Bench::Run("ID.PluginRuntime", &ID::Bench::IDPluginRuntimeSecurityTest, nullptr);

    ID::Bench::EndSession();
    return ID::Bench::ExitCode();
}
