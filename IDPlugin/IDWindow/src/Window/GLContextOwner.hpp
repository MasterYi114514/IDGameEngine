#pragma once

#include <cstdint>

/**
 *  GL 上下文归属诊断（模块私有头，不进公开 include 路径）。
 *
 *  职责：为 `Window` 的 GL 成员提供两件事——**真实绑定查询**（向平台问本线程当前绑定的上下文，
 *  不用缓存猜）与 **owner 诊断**（记录"哪个上下文被认为已绑定、由哪个线程持有"）。
 *
 *  不变量：
 *  1. owner 只做诊断，**不是并发正确性保证**：交接顺序仍由应用负责（实施计划书 §8 的
 *     `release_context()` → 应用自己的屏障 → `make_context_current()`）。owner 与平台真实绑定
 *     冲突时以平台结果为准，并回滚 owner。
 *  2. 状态是本 TU 私有：两个原子量加一个线程私有的线程令牌，定义只在 `GLContextOwner.cpp`，
 *     通过 `GLContextOps` 下的自由函数访问，不交出地址、不跨 TU 可见。
 *  3. 申请 owner 的顺序固定为"先 CAS 转移 → 再调驱动 → 用真实绑定复核"；驱动没有生效时必须
 *     回滚 owner，不得先宣布成功再补校验。
 *  4. 同一上下文被两个线程同时持有属调用方违约：`claim_owner` 返回冲突，调用方**不得**调用驱动、
 *     不得改绑定。
 *  5. 热路径不加锁：全部状态是 relaxed 原子与线程私有量，没有互斥量、没有等待。
 */
namespace ID::Window::Detail
{
    /**
     *  @brief 一次 owner 申请的结果
     *  @note 纯数据载体：`is_conflict` 为真时调用方直接返回；否则用 `previous_context`/`previous_thread`
     *        在驱动复核失败后回滚
     */
    struct GLContextClaim
    {
        bool            is_conflict      = false;       // true = 该上下文已被另一线程持有，调用方不得调用驱动
        void*           previous_context = nullptr;     // 申请前的 owner 上下文；nullptr 表示此前无人持有
        std::uint32_t   previous_thread  = 0;           // 申请前的持有线程令牌；0 表示无
    };

    namespace GLContextOps
    {
        /**
         *  @brief 向平台查询本线程当前绑定的上下文
         *  @return 本线程 current 的上下文；没有绑定时返回 `nullptr`
         *  @note 真实绑定查询，不读任何缓存
         */
        void* get_bound_context() noexcept;

        /**
         *  @brief 绑定/解除本线程的上下文，并用真实绑定查询复核
         *  @param context 目标上下文；`nullptr` 表示解除本线程绑定
         *  @return `true` 表示调用后真实绑定等于 `context`
         *  @note 只影响调用线程；驱动没有生效时返回 `false`，由调用方回滚 owner
         */
        bool bind_context(void* context) noexcept;

        /**
         *  @brief 当前 owner 上下文
         *  @return 被认为已绑定的上下文；`nullptr` 表示无人持有
         */
        void* get_owner() noexcept;

        /**
         *  @brief 尝试把 owner 转给本线程的该上下文
         *  @param context 目标上下文
         *  @return `is_conflict == true` 表示该上下文已被另一线程持有；否则 owner 已属于本线程
         *  @note 成功返回后调用方必须调用驱动并用 `bind_context` 复核，复核失败时调 `rollback_owner`
         */
        GLContextClaim claim_owner(void* context) noexcept;

        /**
         *  @brief 把 owner 回滚到申请前的状态
         *  @param context 本次申请的上下文
         *  @param claim `claim_owner` 的返回值
         *  @note 只回滚仍然指向 `context` 的 owner；已被其它线程改写时不覆盖
         */
        void rollback_owner(void* context, const GLContextClaim& claim) noexcept;

        /**
         *  @brief 本线程已由平台确认绑定成功时补记 owner（只在 owner 为空时记）
         *  @param context 已绑定成功的上下文
         */
        void adopt_owner(void* context) noexcept;

        /**
         *  @brief 本线程刚解除的绑定所对应的 owner 清理
         *  @param unbound_context 本线程刚解除的上下文；`nullptr` 时无操作
         *  @note 只在本线程持有时清空 owner：别的线程持有的 owner 不被本线程改写
         */
        void release_owner_of(void* unbound_context) noexcept;

        /**
         *  @brief 强制清空指向该上下文的 owner
         *  @param context 即将失效的上下文；`nullptr` 时无操作
         *  @note 只用于窗口销毁：调用方必须已按契约停止其它线程对该窗口的使用，因此这里不看持有线程；
         *        否则上下文地址被复用后，新的同地址上下文会被误判为"已被另一线程持有"
         */
        void forget_owner(void* context) noexcept;
    } // namespace GLContextOps
} // namespace ID::Window::Detail
