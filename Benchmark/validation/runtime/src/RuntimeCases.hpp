#pragma once

namespace ID::Bench
{
    /**
     *  @brief 运行期 PluginRuntime 正确性入口（计划书 16.2 的八类覆盖）
     *  @note 本域不进 benchmark.hpp：它只在 ID_BUILD_VALIDATION_RUNTIME=ON 的校验构建里
     *        链接成独立可执行文件，官方 Benchmark.exe 的用例集与 report.txt 不受影响
     */
    void IDPluginRuntimeSecurityTest() noexcept;
} // namespace ID::Bench

namespace ID::Bench::Runtime
{
    void run_lifecycle_cases() noexcept;     // 组 1/2/3/5：四阶段顺序、零插件、init 失败、状态机
    void run_service_cases() noexcept;       // 组 4/6/7：注册期发布违约、槽位边界与所有权、真实调用上游
    void run_plan_cases() noexcept;          // 组 8：计划视图边界
    void run_provision_cases() noexcept;     // 组 9：无槽位能力（kNoSlot）与有槽/无槽混合 provision

    /**
     *  @brief 零插件全空视图的完整生命周期探针（子进程隔离执行）
     *  @note 该视图的 records/topo_order/slot_owner/provisions 全为空，是 PluginRuntime.hpp
     *        明文允许的零插件形态；本探针期望 start 进入 Ready、shutdown 回到 Idle。
     *        同形态用例已经进入主矩阵（zero.null_view.*），这里保留一次独立进程运行，
     *        作为"关闭路径重新引入断言 abort"的回归哨兵：一旦 abort，同进程内后续用例会全部
     *        丢失，无法区分"一个失败"和"全部失败"。探针只做真实条件比较，不放宽期望值。
     */
    void ZeroNullShutdownProbe() noexcept;

    /**
     *  @brief 零插件全空视图的对照探针：只把 records 换成非空静态数组（计数仍是 0）
     *  @note 与 ZeroNullShutdownProbe 唯一差别是 records 是否为 nullptr，用来定位
     *        shutdown 断言里具体是哪一条子句把零插件产品挡住的；同样独立进程运行
     */
    void ZeroNullRecordsShutdownProbe() noexcept;
} // namespace ID::Bench::Runtime
