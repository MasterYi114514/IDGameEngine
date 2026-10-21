#include <array>
#include <cstdio>

#include "FixtureModules.hpp"
#include "FixtureZeroAssembly.generated.hpp"
#include "Kernel/PluginRuntime.hpp"

/**
 *  零插件产品（计划书 12.3）
 *  1. 选中集合是显式给出元素类型与数量的空数组，不依赖空 CTAD；
 *  2. 本可执行文件只链接 ID 与分配桩，不链接任何 fixture 模块 —— 证明零插件产品不依赖 selected 目标；
 *  3. start/shutdown 在 P0 只需能编译、链接、运行并通过空计划校验（运行期状态机属于 P3）。
 */

int main()
{
    using namespace ID::Bench::Fixture;

    constexpr auto plan = sort_and_validate(Generated::make_catalog(), Generated::kSelected);
    static_assert(plan.error == ErrorCode::None, "zero-plugin: 空选中集合必须组装成功");
    static_assert(plan.active_count == 0, "zero-plugin: 不得有任何 active");
    static_assert(plan.edge_count == 0, "zero-plugin: 不得有任何依赖边");

    // 空绑定表：显式给出元素类型与数量
    constexpr std::array<ModuleBinding, 0> binds{};

    // 内核唯一实例：P4 之后是 ID::PluginRuntime（只取地址，不启动运行期状态机）
    const void* kernel = static_cast<const void*>(&ID::PluginRuntime::instance());
    const bool  ok     = kernel != nullptr && binds.size() == 0 && plan.active_count == 0;

    std::printf("fixture-zero-plugin: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
