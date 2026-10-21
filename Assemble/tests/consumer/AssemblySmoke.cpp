#include <cstddef>
#include <cstdio>
#include <string_view>

#include "IDGameEngine.hpp"
#include "Kernel/Memory.hpp"

/**
 *  产品消费 target 的最小冒烟（不是 Benchmark 用例，也不测性能）
 *  1. 只通过 `IDGameEngine` 这个 INTERFACE 消费目标链接：不直接依赖任何插件实现；
 *  2. 显式调用 `ID::Engine::start()` / `shutdown()`：本层不提供静态构造自动启动；
 *  3. 分配桩用**内核自己的统计**核验：大于 8KB 的 new 只能走 ID::Memory 的大对象路径，
 *     宿主 new 若被 CRT 接管，内核的大对象字节数与保留字节数都不会变化；
 *     小对象 new/delete 只作为“能正常分配释放”的事实记录，不声称路由结论；
 *  4. 零插件产品同样走这一条路径：start 从 Idle 进入 Ready，shutdown 回到 Idle。
 */

namespace
{
    // 大于 kMaxSmallBlockSize(8KB)：走大对象映射，内核按整段映射字节数记账
    constexpr std::size_t kProofBytes = 1u << 20;
} // namespace

int main()
{
    ID::PluginRuntimeDiagnostic started = ID::Engine::start();
    if (!started.is_ok())
    {
        const std::string_view code = ID::plugin_runtime_error_name(started.code);
        const std::string_view state = ID::plugin_runtime_state_name(started.state);
        std::printf("[FAIL] start code=%.*s state=%.*s\n",
            static_cast<int>(code.size()), code.data(),
            static_cast<int>(state.size()), state.data());
        return 1;
    }

    const bool ready = ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Ready;

    // ---- 小对象：只记录“能分配、能取值、能释放” ----
    int* const scalar = new int(42);
    const bool scalar_ok = scalar != nullptr && *scalar == 42;
    delete scalar;

    // ---- 大对象：内核记账是可见证据 ----
    const ID::Memory::MemoryStats before = ID::Memory::ID_GetMemoryStats();

    std::byte* const proof = new std::byte[kProofBytes];
    if (proof == nullptr)
    {
        std::printf("[FAIL] host large new returned nullptr\n");
        return 1;
    }
    // volatile 写让分配地址真正逃逸，避免优化器把 new/delete 整对消除
    volatile std::byte* sink = proof;
    sink[0] = std::byte{ 0x5A };
    sink[kProofBytes - 1] = std::byte{ 0xA5 };

    const ID::Memory::MemoryStats during = ID::Memory::ID_GetMemoryStats();
    delete[] proof;
    const ID::Memory::MemoryStats after = ID::Memory::ID_GetMemoryStats();

    const std::size_t grown    = during.large_allocation_bytes > before.large_allocation_bytes
                                     ? during.large_allocation_bytes - before.large_allocation_bytes
                                     : 0;
    const bool        routed   = grown >= kProofBytes;
    const bool        released = after.large_allocation_bytes == before.large_allocation_bytes
                                 && after.total_reserved_bytes == before.total_reserved_bytes;

    ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
    const bool idle = ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Idle;

    std::printf("[%s] start state=Ready\n", ready ? "PASS" : "FAIL");
    std::printf("[%s] host small new/delete completed (alloc and free succeeded)\n", scalar_ok ? "PASS" : "FAIL");
    std::printf("[%s] host large new/delete accounted by kernel stats (grew=%llu bytes, released=%d)\n",
        (routed && released) ? "PASS" : "FAIL",
        static_cast<unsigned long long>(grown),
        released ? 1 : 0);
    std::printf("[%s] shutdown state=Idle (code=%s)\n",
        (stopped.is_ok() && idle) ? "PASS" : "FAIL",
        stopped.is_ok() ? "None" : "error");

    return (ready && scalar_ok && routed && released && stopped.is_ok() && idle) ? 0 : 1;
}
