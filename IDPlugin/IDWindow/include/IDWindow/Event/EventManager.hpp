#pragma once

#include "IDWindowCore.hpp"

/**
 *  事件层状态：只有"是否已初始化"与"是否启用"两个布尔状态。
 *
 *  边界：
 *  1. 没有回调表、没有事件队列、没有事件缓冲，不转发任何东西，不知道窗口，也不是 `Window` 的友元。
 *  2. 状态放在实现文件的匿名命名空间（`g_` 前缀），只经本类的 `static` 函数访问，
 *     因此不产生跨 TU 可见的可变全局，也不需要单例实例。
 *  3. 开关的唯一读取点是 `Window::on_event`；关闭时事件静默丢弃，不缓存、不计数。
 *  4. `init` / `shutdown` / `enable` / `disable` 在事件回调栈内调用属调用方违约：写日志、不改现态；
 *     两个 `bool` 查询在回调栈内调用是允许的。
 *  5. 生命周期与开关只在主线程调用；两个 `bool` 查询可在任意线程只读观察。
 *  6. 两个状态字是 `std::atomic<bool>`，读写一律 `relaxed`：原子性只用来消除跨线程读写的未定义行为，
 *     不建立线程间的同步或顺序关系，也不扩大上面的线程前提。
 */
namespace ID::Window::Event
{
    class ID_WINDOW_API EventManager
    {
    public:
        EventManager() = delete;                 // 只提供静态接口，不允许实例化

        /**
         *  @brief 初始化事件层，并把事件闸门置为打开
         *  @note 由 `ID::Window::init` 调用；重复调用只写一条日志，不改现态
         */
        static void init() noexcept;

        /**
         *  @brief 关闭事件层
         *  @note 由 `ID::Window::shutdown` 调用；未初始化时是无操作
         */
        static void shutdown() noexcept;

        /**
         *  @brief 查询事件层是否已初始化
         *  @return 已初始化时返回 true
         */
        static bool is_initialized() noexcept;

        /**
         *  @brief 打开事件闸门
         *  @note 未初始化时写日志、不改现态
         */
        static void enable() noexcept;

        /**
         *  @brief 关闭事件闸门：此后所有窗口的事件都被静默丢弃
         */
        static void disable() noexcept;

        /**
         *  @brief 查询事件闸门是否打开
         *  @return 已启用时返回 true
         */
        static bool is_enabled() noexcept;
    };
} // namespace ID::Window::Event
