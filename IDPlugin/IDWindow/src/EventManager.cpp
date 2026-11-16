#include "IDWindow/Event/EventManager.hpp"

#include <atomic>

#include "Core/Core.hpp"
#include "DispatchScope.hpp"
#include "Log/Log.hpp"

/**
 *  事件层状态实现：两个 `std::atomic<bool>` 状态字放在本文件的匿名命名空间，只经 `EventManager` 的静态函数访问。
 *
 *  不变量：
 *  1. 没有对象状态、没有单例、没有回调表与队列：本文件的 `g_` 变量不交出地址，也不跨 TU 可见。
 *  2. 两个状态字一律按 `relaxed` 读写：它们只在主线程被改写、被任意线程只读观察，彼此之间没有
 *     发布-订阅关系，用原子只为消除跨线程读写的未定义行为，不建立同步或顺序保证。
 *  3. 初始化后事件闸门默认打开；`disable()` 之后所有窗口的事件被 `Window::on_event` 静默丢弃。
 *  4. `init` / `shutdown` / `enable` / `disable` 在事件回调栈内调用属调用方违约：只写日志、不改现态，
 *     判定来自 `src/DispatchScope.cpp` 的派发深度。
 *  5. 失败与违约诊断只写日志（日志器未就绪时由日志封装静默丢弃），不改现态；这些记录全部落在
 *     `ID_NOINLINE` + `ID_COLD` 冷函数里，四个改状态的入口在正常调用下不进冷函数。
 */
namespace ID::Window::Event
{
    namespace
    {
        std::atomic<bool> g_is_initialized{ false };    // 事件层是否已初始化
        std::atomic<bool> g_is_enabled{ false };        // 事件闸门；只有初始化后才可能为 true

        // ---- 失败冷路径：不内联、集中放置，四个改状态的入口在正常调用下不碰日志 ----

        ID_NOINLINE ID_COLD void report_init_already_initialized() noexcept
        {
            IDWINDOW_LOG_WARN("EventManager::init: 事件层已初始化，本次调用不改变状态");
        }

        ID_NOINLINE ID_COLD void report_enable_uninitialized() noexcept
        {
            IDWINDOW_LOG_ERROR("EventManager::enable: 事件层尚未初始化，本次调用不改变状态");
        }

        ID_NOINLINE ID_COLD void report_disable_uninitialized() noexcept
        {
            IDWINDOW_LOG_ERROR("EventManager::disable: 事件层尚未初始化，本次调用不改变状态");
        }
    } // namespace

    void EventManager::init() noexcept
    {
        if (::ID::Window::Detail::reject_if_in_callback("EventManager::init"))
        {
            return;
        }

        if (g_is_initialized.load(std::memory_order_relaxed))
        {
            report_init_already_initialized();
            return;
        }

        g_is_initialized.store(true, std::memory_order_relaxed);
        g_is_enabled.store(true, std::memory_order_relaxed);

        IDWINDOW_LOG_TRACE("EventManager::init: 事件层已初始化，事件闸门打开");
    }

    void EventManager::shutdown() noexcept
    {
        if (::ID::Window::Detail::reject_if_in_callback("EventManager::shutdown"))
        {
            return;
        }

        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            return;
        }

        g_is_initialized.store(false, std::memory_order_relaxed);
        g_is_enabled.store(false, std::memory_order_relaxed);

        IDWINDOW_LOG_TRACE("EventManager::shutdown: 事件层已关闭");
    }

    bool EventManager::is_initialized() noexcept
    {
        return g_is_initialized.load(std::memory_order_relaxed);
    }

    void EventManager::enable() noexcept
    {
        if (::ID::Window::Detail::reject_if_in_callback("EventManager::enable"))
        {
            return;
        }

        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            report_enable_uninitialized();
            return;
        }

        g_is_enabled.store(true, std::memory_order_relaxed);

        IDWINDOW_LOG_TRACE("EventManager::enable: 事件闸门打开");
    }

    void EventManager::disable() noexcept
    {
        if (::ID::Window::Detail::reject_if_in_callback("EventManager::disable"))
        {
            return;
        }

        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            report_disable_uninitialized();
            return;
        }

        g_is_enabled.store(false, std::memory_order_relaxed);

        IDWINDOW_LOG_TRACE("EventManager::disable: 事件闸门关闭");
    }

    bool EventManager::is_enabled() noexcept
    {
        return g_is_enabled.load(std::memory_order_relaxed);
    }
} // namespace ID::Window::Event
