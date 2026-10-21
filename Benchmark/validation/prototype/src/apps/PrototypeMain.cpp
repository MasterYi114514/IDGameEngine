#include <cstdio>

#include "FixtureAlphaApi.hpp"
#include "FixtureBetaApi.hpp"
#include "FixtureModules.hpp"
#include "Kernel/PluginRuntime.hpp"

/**
 *  P0 原型宿主（两个模块的 STATIC 或 SHARED 产品）
 *  1. 只通过产品入口启动/关闭，验证"归档里的入口被提取 + 回调真实执行"；
 *  2. 校验上游能力被下游真正调用、四个回调各执行一次；
 *  3. 校验模块与宿主看到的是同一个 ID 内核单例；
 *  4. 触发一次普通 new/delete，验证分配桩在本 EXE 与模块里都接上了内核。
 *  @note 这是校验程序，不是 Benchmark 测试域：输出只走 stdout，不写 report.txt，
 *        也不链接 ID::Bench 支持库（否则会多出一个报告写入者）。
 */

int main()
{
    using namespace ID::Bench::Fixture;

    bool ok = FixtureProductStart();

    const ModuleState* alpha_state = FixtureAlphaState();
    const ModuleState* beta_state  = FixtureBetaState();
    ok = ok && alpha_state != nullptr && beta_state != nullptr;
    if (alpha_state != nullptr && beta_state != nullptr)
    {
        ok = ok && alpha_state->on_register_count == 1 && alpha_state->on_init_count == 1;
        ok = ok && beta_state->on_register_count == 1 && beta_state->on_init_count == 1;
        // 下游真的通过上游公开能力表算过一次：21 * 2654435761 + 17
        ok = ok && beta_state->last_upstream_value == (21u * 2654435761u + 17u);
    }

    // 唯一内核实例：模块导出的单例地址必须与宿主一致
    // @note P0 写这条探针时取的是当时的运行期单例；P4 退役 PluginManager 运行期接口后，
    //       内核唯一实例是 ID::PluginRuntime，探针语义（跨模块只有一个内核实例）保持不变。
    //       探针只取地址，不 start/shutdown，因此不改动本原型自己的四阶段绑定表语义。
    const void* kernel       = static_cast<const void*>(&ID::PluginRuntime::instance());
    const void* alpha_kernel = FixtureAlphaKernelInstance();
    const void* beta_kernel  = FixtureBetaKernelInstance();
    ok = ok && alpha_kernel == kernel;
    ok = ok && beta_kernel == kernel;
    std::printf("kernel-instance: host=%p alpha=%p beta=%p shared=%s (ID::PluginRuntime singleton)\n",
                kernel, alpha_kernel, beta_kernel,
                (alpha_kernel == kernel && beta_kernel == kernel) ? "yes" : "no");

    // 宿主的普通 new/delete 必须走内核分配桩：volatile 读让地址逃逸，避免整对分配被优化掉
    int*          probe = new int(7);
    volatile int* sink  = probe;
    ok = ok && sink != nullptr && *sink == 7;
    delete probe;

    // 每个模块各自接桩：模块从不分配时链接器会丢掉桩对象，所以必须显式探测
    ok = ok && FixtureAlphaHeapProbe();
    ok = ok && FixtureBetaHeapProbe();

    FixtureProductStop();
    if (alpha_state != nullptr && beta_state != nullptr)
    {
        ok = ok && alpha_state->on_shutdown_count == 1 && alpha_state->on_unload_count == 1;
        ok = ok && beta_state->on_shutdown_count == 1 && beta_state->on_unload_count == 1;
    }

    std::printf("fixture-prototype: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
