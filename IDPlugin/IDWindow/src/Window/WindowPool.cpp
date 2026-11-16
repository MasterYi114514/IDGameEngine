#include "IDWindow/WindowPool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

#include "DispatchScope.hpp"
#include "IDWindow.hpp"
#include "IDWindowLifeCycle.hpp"
#include "Log/Log.hpp"

/**
 *  窗口池实现：固定 64 槽的 `std::byte` 存储 + placement new + 显式析构 + 自由链。
 *
 *  不变量：
 *  1. 存储一旦分配就固定：窗口对象地址 = 存储起点 + 槽下标 × `sizeof(Window)`，不扩容、不搬移，
 *     因此"存活期间的 `Window&` 地址稳定"成立。
 *  2. 地址只按 `reinterpret_cast<Window*>` 计算，**不在未构造的存储上调用 `std::launder`**
 *     （它的前置条件是存储上已有同类型对象存活）。
 *  3. 构造与析构的调用点只能写在 `WindowPool` 的成员函数体内：`friend class WindowPool` 的授权
 *     只对池的成员生效，匿名命名空间里的自由函数调不了 `Window` 的私有构造与私有析构。
 *  4. 本文件不包含任何平台头、不读 `RenderApi` tag、不解释描述符的 `union`。
 *  5. 失败路径只写日志与错误文本，不改空闲链、不重复析构。
 *  6. 生命周期日志：一次窗口创建与一次窗口销毁各只产生一条 INFO，该记录由 `Window::open_platform` /
 *     `Window::destroy` 给出；本文件不重复记录同一事件，`shutdown` 的批量扫描只记一条 TRACE。
 */
namespace ID::Window
{
    namespace
    {
        constexpr std::uint32_t kMaxWindows  = 64;
        constexpr std::uint32_t kInvalidSlot = 0xFFFFFFFFu;

        alignas(Window) std::array<std::byte, kMaxWindows * sizeof(Window)> g_window_storage{};
        std::array<bool, kMaxWindows>           g_is_occupied{};        // 槽当前是否有已构造的 Window
        std::array<std::uint32_t, kMaxWindows>  g_free_next{};
        std::uint32_t                           g_free_head    = kInvalidSlot;
        std::uint32_t                           g_current_slot = kInvalidSlot;

        // 只算地址：不构造、不析构，因此不需要 Window 的私有访问权
        Window* slot_address(std::uint32_t index) noexcept
        {
            return reinterpret_cast<Window*>(g_window_storage.data() + index * sizeof(Window));
        }

        bool fits_platform_int(std::uint32_t value) noexcept
        {
            return value <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());
        }

        void reset_free_chain() noexcept
        {
            for (std::uint32_t index = 0; index < kMaxWindows; ++index)
            {
                g_is_occupied[index] = false;
                g_free_next[index]   = (index + 1 < kMaxWindows) ? (index + 1) : kInvalidSlot;
            }

            g_free_head    = 0;
            g_current_slot = kInvalidSlot;
        }

        // ---- 失败冷路径：不内联、集中放置，热路径上没有日志 ----

        ID_NOINLINE ID_COLD void report_create_uninitialized() noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::create_window: 窗口子系统尚未初始化");
            IDWINDOW_LOG_ERROR("WindowPool::create_window: 窗口子系统尚未初始化，返回无效句柄");
        }

        ID_NOINLINE ID_COLD void report_create_null_title() noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::create_window: 描述符的 title 为 nullptr");
            IDWINDOW_LOG_ERROR("WindowPool::create_window: 描述符的 title 为 nullptr，返回无效句柄");
        }

        ID_NOINLINE ID_COLD void report_create_invalid_size(std::uint32_t width, std::uint32_t height) noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::create_window: 描述符的尺寸为 0 或超出平台整数表达范围");
            IDWINDOW_LOG_ERROR("WindowPool::create_window: 描述符尺寸 {}x{} 为 0 或超出平台整数表达范围，返回无效句柄", width, height);
        }

        ID_NOINLINE ID_COLD void report_pool_full() noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::create_window: 固定槽位已满");
            IDWINDOW_LOG_ERROR("WindowPool::create_window: 固定槽位已满（共 {} 槽），返回无效句柄", kMaxWindows);
        }

        ID_NOINLINE ID_COLD void report_open_platform_failed(std::uint32_t index) noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::create_window: 平台窗口创建失败，槽位已回收");
            IDWINDOW_LOG_ERROR("WindowPool::create_window: 槽 {} 的平台窗口创建失败，槽位已回收", index);
        }

        ID_NOINLINE ID_COLD void report_destroy_violation(std::uint16_t handle_id) noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::destroy_window: 句柄无效、越界或指向空闲槽");
            IDWINDOW_LOG_ERROR("WindowPool::destroy_window: 句柄 {} 无效、越界或指向空闲槽，已忽略", handle_id);
        }

        ID_NOINLINE ID_COLD void report_set_current_violation(std::uint16_t handle_id) noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::set_current_window: 句柄无效、越界或指向空闲槽");
            IDWINDOW_LOG_ERROR("WindowPool::set_current_window: 句柄 {} 无效、越界或指向空闲槽，current 保持不变", handle_id);
        }

        ID_NOINLINE ID_COLD void report_no_current() noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::get_current_window: 没有 current 窗口");
            IDWINDOW_LOG_ERROR("WindowPool::get_current_window: 没有 current 窗口，属调用方违约");
        }

        ID_NOINLINE ID_COLD void report_get_window_violation(std::uint16_t handle_id) noexcept
        {
            ::ID::Window::Detail::set_error_text("WindowPool::get_window: 句柄无效、越界或指向空闲槽");
            IDWINDOW_LOG_ERROR("WindowPool::get_window: 句柄 {} 无效、越界或指向空闲槽，属调用方违约", handle_id);
        }
    } // namespace

    WindowHandle WindowPool::create_window(const WindowDesc& desc) noexcept
    {
        if (!ID::Window::is_initialized())
        {
            report_create_uninitialized();
            return WindowHandle::invalid_handle();
        }

        if (desc.title == nullptr)
        {
            report_create_null_title();
            return WindowHandle::invalid_handle();
        }

        if (desc.size.width == 0 || desc.size.height == 0
            || !fits_platform_int(desc.size.width) || !fits_platform_int(desc.size.height))
        {
            report_create_invalid_size(desc.size.width, desc.size.height);
            return WindowHandle::invalid_handle();
        }

        if (g_free_head == kInvalidSlot)
        {
            report_pool_full();
            return WindowHandle::invalid_handle();
        }

        const std::uint32_t index  = g_free_head;
        Window* const       window = slot_address(index);

        ::new (static_cast<void*>(window)) Window(desc, WindowHandle{static_cast<WindowUINT>(index)});

        g_is_occupied[index] = true;
        g_free_head          = g_free_next[index];
        g_free_next[index]   = kInvalidSlot;

        if (!window->open_platform())
        {
            window->destroy();
            window->~Window();

            g_is_occupied[index] = false;
            g_free_next[index]   = g_free_head;
            g_free_head          = index;

            report_open_platform_failed(index);
            return WindowHandle::invalid_handle();
        }

        // 创建记录由 `Window::open_platform` 记一条 INFO：同一事件不在两处重复占用 INFO
        return WindowHandle{static_cast<WindowUINT>(index)};
    }

    void WindowPool::destroy_window(WindowHandle handle) noexcept
    {
        if (Detail::reject_if_in_callback("WindowPool::destroy_window"))
        {
            return;
        }

        const std::uint32_t index = static_cast<std::uint32_t>(handle.get_id());

        if (!handle.is_valid() || index >= kMaxWindows || !g_is_occupied[index])
        {
            report_destroy_violation(handle.get_id());
            return;
        }

        Window* const window = slot_address(index);

        if (g_current_slot == index)
        {
            g_current_slot = kInvalidSlot;
        }

        window->destroy();
        window->~Window();

        g_is_occupied[index] = false;
        g_free_next[index]   = g_free_head;
        g_free_head          = index;

        // 销毁记录由 `Window::destroy` 记一条 INFO：同一事件不在两处重复占用 INFO
    }

    void WindowPool::set_current_window(WindowHandle handle) noexcept
    {
        const std::uint32_t index = static_cast<std::uint32_t>(handle.get_id());

        if (!handle.is_valid() || index >= kMaxWindows || !g_is_occupied[index])
        {
            report_set_current_violation(handle.get_id());
            return;
        }

        // 不写日志：本接口属窗口操作，调用频率由应用决定（可能逐帧切换窗口），无法判定为低频，
        // 因此与其它窗口操作一样保持零日志；其效果可由 `get_current_window()` 直接观测。
        g_current_slot = index;
    }

    Window& WindowPool::get_current_window() noexcept
    {
        if (g_current_slot == kInvalidSlot || !g_is_occupied[g_current_slot])
        {
            report_no_current();
            ID_DEBUG_BREAK();       // 调用方违约且没有错误通道：终止进程，与是否 debug 构建无关
            std::abort();           // MSVC 的 ID_DEBUG_BREAK 不是 noreturn，这里补上确定的终止路径
        }

        return *slot_address(g_current_slot);
    }

    Window& WindowPool::get_window(WindowHandle handle) noexcept
    {
        const std::uint32_t index = static_cast<std::uint32_t>(handle.get_id());

        if (!handle.is_valid() || index >= kMaxWindows || !g_is_occupied[index])
        {
            report_get_window_violation(handle.get_id());
            ID_DEBUG_BREAK();       // 调用方违约且没有错误通道：终止进程，与是否 debug 构建无关
            std::abort();           // MSVC 的 ID_DEBUG_BREAK 不是 noreturn，这里补上确定的终止路径
        }

        return *slot_address(index);
    }

    namespace LifeCycle::OnInit
    {
        void reset_window_pool() noexcept
        {
            reset_free_chain();
            IDWINDOW_LOG_TRACE("WindowPool::reset_window_pool: 槽表与自由链已复位");
        }
    } // namespace LifeCycle::OnInit

    namespace LifeCycle::OnShutdown
    {
        void destroy_window_pool() noexcept
        {
            std::uint32_t destroyed = 0;

            for (std::uint32_t index = 0; index < kMaxWindows; ++index)
            {
                if (!g_is_occupied[index])
                {
                    continue;
                }

                // 逐槽等价于对存活句柄调用 destroy_window：析构调用点因此仍留在池的成员函数体内
                WindowPool::destroy_window(slot_address(index)->get_handle());
                ++destroyed;
            }

            reset_free_chain();

            // 逐窗口的销毁记录由 `Window::destroy` 给出；这里只记"扫描完成"这一步，不重复占用 INFO
            IDWINDOW_LOG_TRACE("WindowPool::destroy_window_pool: shutdown 扫描完成，共销毁 {} 个存活窗口，槽表与自由链已复位", destroyed);
        }
    } // namespace LifeCycle::OnShutdown
} // namespace ID::Window
