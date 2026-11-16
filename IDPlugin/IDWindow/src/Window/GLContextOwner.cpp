#include "Window/GLContextOwner.hpp"

#include <atomic>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/**
 *  GL 上下文归属诊断的实现：owner 原子 + 真实绑定查询。
 *
 *  不变量：
 *  1. `g_owner_context` 记录"被认为已绑定"的上下文，`g_owner_thread` 记录持有它的线程令牌
 *     （0 表示无主）；两者都是 relaxed 原子，只用于诊断，不作为并发正确性保证。
 *  2. 线程令牌是线程私有量（`g_thread_token`），首次使用时从 `g_next_thread_token` 取一个非零值；
 *     0 保留给"无主"。
 *  3. 真实绑定只经 `glfwGetCurrentContext()` 取当次值：任何判定都以平台结果为准，
 *     不拿 owner 代替真实绑定。
 *  4. 驱动调用与复核都由 `bind_context` 一处完成：调用方只在它返回 `false` 时回滚 owner。
 *  5. 本文件不含日志、不写错误文本：失败诊断由调用方（`Window.cpp`）的冷路径负责，
 *     使 owner 诊断本身可以同时被多个入口复用。
 */
namespace ID::Window::Detail
{
    namespace
    {
        std::atomic<void*>          g_owner_context{ nullptr };     // 被认为已绑定的上下文；nullptr = 无人持有
        std::atomic<std::uint32_t>  g_owner_thread{ 0 };            // 持有 g_owner_context 的线程令牌；0 = 无主
        std::atomic<std::uint32_t>  g_next_thread_token{ 1 };       // 线程令牌发号器；0 保留给"无主"

        thread_local std::uint32_t  g_thread_token = 0;             // 本线程令牌；首次使用时分配

        std::uint32_t thread_token() noexcept
        {
            if (g_thread_token == 0)
            {
                g_thread_token = g_next_thread_token.fetch_add(1, std::memory_order_relaxed);
            }
            return g_thread_token;
        }
    } // namespace

    namespace GLContextOps
    {
        void* get_bound_context() noexcept
        {
            return static_cast<void*>(glfwGetCurrentContext());
        }

        bool bind_context(void* context) noexcept
        {
            glfwMakeContextCurrent(static_cast<GLFWwindow*>(context));

            // 驱动没有生效时平台侧的 current 不变：以这次的复查结果决定成败，不回读 owner
            return get_bound_context() == context;
        }

        void* get_owner() noexcept
        {
            return g_owner_context.load(std::memory_order_relaxed);
        }

        GLContextClaim claim_owner(void* context) noexcept
        {
            GLContextClaim      claim;
            const std::uint32_t token = thread_token();

            void* owner = g_owner_context.load(std::memory_order_relaxed);
            for (;;)
            {
                if (owner == context)
                {
                    // 同一上下文：持有线程是本线程才继续（同一线程重取），否则属"两个线程同时持有"
                    const std::uint32_t owner_thread = g_owner_thread.load(std::memory_order_relaxed);
                    claim.previous_context = owner;
                    claim.previous_thread  = owner_thread;

                    if (owner_thread != 0 && owner_thread != token)
                    {
                        claim.is_conflict = true;
                        return claim;
                    }

                    g_owner_thread.store(token, std::memory_order_relaxed);
                    return claim;
                }

                // 无主、或 owner 是别的上下文（本线程换绑或别的线程另有绑定）：把 owner 转给本线程
                if (g_owner_context.compare_exchange_weak(owner, context, std::memory_order_relaxed))
                {
                    claim.previous_context = owner;
                    claim.previous_thread  = g_owner_thread.exchange(token, std::memory_order_relaxed);
                    return claim;
                }
            }
        }

        void rollback_owner(void* context, const GLContextClaim& claim) noexcept
        {
            void* owner = context;

            // 只有 owner 仍指向本次申请的上下文时才回滚：已被别的线程改写时不覆盖
            if (g_owner_context.compare_exchange_strong(owner, claim.previous_context, std::memory_order_relaxed))
            {
                g_owner_thread.store(claim.previous_thread, std::memory_order_relaxed);
            }
        }

        void adopt_owner(void* context) noexcept
        {
            void* owner = nullptr;

            // 只在无主时补记：已有其它 owner 时不抢
            if (g_owner_context.compare_exchange_strong(owner, context, std::memory_order_relaxed))
            {
                g_owner_thread.store(thread_token(), std::memory_order_relaxed);
            }
        }

        void release_owner_of(void* unbound_context) noexcept
        {
            if (unbound_context == nullptr)
            {
                return;
            }

            // 别的线程持有的 owner 不由本线程清除
            const std::uint32_t owner_thread = g_owner_thread.load(std::memory_order_relaxed);
            if (owner_thread != 0 && owner_thread != thread_token())
            {
                return;
            }

            void* owner = unbound_context;
            if (g_owner_context.compare_exchange_strong(owner, nullptr, std::memory_order_relaxed))
            {
                g_owner_thread.store(0, std::memory_order_relaxed);
            }
        }

        void forget_owner(void* context) noexcept
        {
            if (context == nullptr)
            {
                return;
            }

            void* owner = context;
            if (g_owner_context.compare_exchange_strong(owner, nullptr, std::memory_order_relaxed))
            {
                g_owner_thread.store(0, std::memory_order_relaxed);
            }
        }
    } // namespace GLContextOps
} // namespace ID::Window::Detail
