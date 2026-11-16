#include "IDWindow.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <string_view>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "DispatchScope.hpp"
#include "IDWindow/Event/EventManager.hpp"
#include "IDWindowLifeCycle.hpp"
#include "Log/Log.hpp"

/**
 *  插件入口实现：进程级生命周期编排、错误文本通道与事件泵。
 *
 *  不变量：
 *  1. `init()` 顺序固定：① `ID::Log::init()` → ② 本模块日志器句柄 → ③ 平台错误回调 →
 *     ④ 平台进程级初始化 → ⑤ 事件层 → ⑥ 复位窗口池。任一步失败时按**已建立项逆序回滚**：
 *     ④ 失败回滚 ③（清错误回调）与 ②（销毁日志器句柄），② 失败时 ① 不归本插件回滚
 *     （日志插件可能被其它插件共用）；⑤ ⑥ 不返回失败，因此没有后续回滚分支。
 *  2. `shutdown()` 顺序固定：先销毁全部存活窗口（平台要求终止前所有窗口已销毁）→ 关闭事件层 →
 *     销毁本模块自己的日志器句柄 → 撤错误回调 → 平台终止 → 复位对外状态。槽表复位在销毁全部窗口的
 *     同一步完成：此时已无存活窗口，先复位不影响终止调用。
 *  3. 错误文本用本线程的定长缓冲承载，写入只做定长字节拷贝：日志器未就绪时该通道照常可用。
 *  4. 平台类型只出现在本文件与 `src/Window/Window.cpp`；公开头只有插件自有类型。
 *  5. 一个生命周期事件只有一条 INFO：每个窗口的创建与销毁各由 `Window::open_platform` /
 *     `Window::destroy` 记一条 INFO，`WindowPool` 不重复记录同一事件；`shutdown` 的批量扫描只记 TRACE。
 */
#define ID_WINDOW_DETAIL_STRINGIZE_IMPL(value) #value
#define ID_WINDOW_DETAIL_STRINGIZE(value) ID_WINDOW_DETAIL_STRINGIZE_IMPL(value)

namespace ID::Window
{
    namespace
    {
        constexpr std::size_t kErrorTextCapacity = 512;

        std::atomic<bool> g_is_initialized{ false };                    // 对外可观察的就绪标志
        thread_local char g_error_text[kErrorTextCapacity] = {};        // 本线程的错误文本缓冲

        constexpr char kVersionString[] =
            "IDWindow " ID_WINDOW_DETAIL_STRINGIZE(ID_WINDOW_VERSION_MAJOR) "."
            ID_WINDOW_DETAIL_STRINGIZE(ID_WINDOW_VERSION_MINOR) " (GLFW "
            ID_WINDOW_DETAIL_STRINGIZE(GLFW_VERSION_MAJOR) "."
            ID_WINDOW_DETAIL_STRINGIZE(GLFW_VERSION_MINOR) "."
            ID_WINDOW_DETAIL_STRINGIZE(GLFW_VERSION_REVISION) ")";

        static_assert(ID_WINDOW_INTERFACE_VERSION == 2,
                      "接口版本必须是 2：本次是破坏性契约变更，回退版本号即违约");
        static_assert(std::string_view{ kVersionString }.find("IDWindow") == 0,
                      "version_string 必须以 IDWindow 开头，否则版本身份串无法定位插件");
        static_assert(std::string_view{ kVersionString }.find(
                          ID_WINDOW_DETAIL_STRINGIZE(ID_WINDOW_VERSION_MAJOR) "."
                          ID_WINDOW_DETAIL_STRINGIZE(ID_WINDOW_VERSION_MINOR)) != std::string_view::npos,
                      "version_string 必须包含由 ID_WINDOW_VERSION_MAJOR/MINOR 拼出的版本号，二者不一致时本断言失败");

        void set_error_text_impl(const char* prefix, const char* text) noexcept
        {
            std::size_t length = 0;

            if (prefix != nullptr)
            {
                for (std::size_t index = 0; prefix[index] != '\0' && length + 1 < kErrorTextCapacity; ++index)
                {
                    g_error_text[length] = prefix[index];
                    ++length;
                }
            }

            if (text != nullptr)
            {
                for (std::size_t index = 0; text[index] != '\0' && length + 1 < kErrorTextCapacity; ++index)
                {
                    g_error_text[length] = text[index];
                    ++length;
                }
            }

            g_error_text[length] = '\0';
        }

        // 平台错误回调：只做定长字节拷贝，不格式化、不取日志器（日志器可能尚未就绪）
        void error_callback(int /*error_code*/, const char* description) noexcept
        {
            set_error_text_impl("GLFW: ", description);
        }

        // ---- 失败冷路径：不内联、集中放置 ----

        // init 步骤 ②（日志器句柄）失败：此时还没有任何需要回滚的其它资源——步骤 ① 的 `ID::Log::init()`
        // 幂等且不归本插件回滚，日志器句柄在创建失败时已由 `IDWindow_logger_init()` 自己清掉。
        // 本分支不得调用任何日志宏：日志器未就绪，只走错误文本通道。
        ID_NOINLINE ID_COLD bool rollback_init_logger_failed() noexcept
        {
            Detail::set_error_text("ID::Window::init: 本模块日志器未就绪，初始化已停止");
            return false;
        }

        // init 步骤 ④（平台进程级初始化）失败：按已建立项逆序回滚 ③ 与 ②。
        // 不调用 `glfwTerminate()`：vendor `GLFW/glfw3.h:2235-2237`（`glfwTerminate` 的文档段）原文为
        // "If initialization fails, there is no need to call this function, as it is called by
        // @ref glfwInit before it returns failure."，`glfwInit` 的文档段（`glfw3.h:2174`）同时写明
        // "If this function fails, it calls @ref glfwTerminate before returning."；
        // 平台侧清理已由 `glfwInit` 自己完成，再调一次只是重复动作。
        ID_NOINLINE ID_COLD bool rollback_init_platform_failed() noexcept
        {
            // 先写这一条日志、再销毁日志器句柄：两步顺序一旦交换，这条诊断就丢了
            IDWINDOW_LOG_ERROR("ID::Window::init: 平台进程级初始化失败，开始逆序回滚（清错误回调 → 销毁日志器句柄）");

            // 步骤 ③ 的逆操作：撤掉平台错误回调，此后 GLFW 不再向本插件写错误文本
            glfwSetErrorCallback(nullptr);

            // 步骤 ② 的逆操作：销毁本模块自己的日志器句柄并把日志器指针置空
            LifeCycle::OnShutdown::IDWindow_logger_shutdown();

            return false;
        }

        ID_NOINLINE ID_COLD void report_pump_uninitialized(const char* function_name) noexcept
        {
            Detail::set_error_text("窗口子系统尚未初始化，事件泵未转发");
            IDWINDOW_LOG_ERROR("{}: 窗口子系统尚未初始化，本次调用未转发", function_name);
        }

        ID_NOINLINE ID_COLD void report_invalid_timeout(double timeout_seconds) noexcept
        {
            Detail::set_error_text("wait_events 的超时秒数非法：必须为有限正数");
            IDWINDOW_LOG_WARN("ID::Window::wait_events: 超时秒数 {} 非法（必须为有限正数），本次不等待", timeout_seconds);
        }
    } // namespace

    namespace Detail
    {
        void set_error_text(const char* text) noexcept
        {
            set_error_text_impl(nullptr, text);
        }
    } // namespace Detail

    const char* version_string() noexcept
    {
        return kVersionString;
    }

    bool init()
    {
        if (Detail::reject_if_in_callback("ID::Window::init"))
        {
            return false;
        }

        // 幂等：已初始化时直接成功，不重复初始化，也不清空本线程既有错误文本
        if (g_is_initialized.load(std::memory_order_relaxed))
        {
            return true;
        }

        // 步骤 ①：日志插件初始化（幂等、没有失败返回；失败只会在步骤 ② 的句柄检查上体现）
        LifeCycle::OnInit::IDLog_init();

        // 步骤 ②：本模块日志器句柄
        if (!LifeCycle::OnInit::IDWindow_logger_init())
        {
            return rollback_init_logger_failed();       // 此分支不得调用任何日志宏
        }

        // 步骤 ③：平台错误回调必须在进程级初始化之前安装，否则拿不到初始化期错误
        glfwSetErrorCallback(error_callback);

        // 步骤 ④：平台进程级初始化
        if (glfwInit() != GLFW_TRUE)
        {
            return rollback_init_platform_failed();
        }

        // 步骤 ⑤ ⑥：事件层与窗口池都不返回失败，因此没有对应的回滚分支
        Event::EventManager::init();
        LifeCycle::OnInit::reset_window_pool();

        g_is_initialized.store(true, std::memory_order_relaxed);

        IDWINDOW_LOG_INFO("ID::Window::init: 窗口子系统已初始化");
        return true;
    }

    void shutdown() noexcept
    {
        if (Detail::reject_if_in_callback("ID::Window::shutdown"))
        {
            return;
        }

        // 幂等：未初始化时是无操作（不写日志、不改错误文本），之后可以再次 init()
        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            return;
        }

        // 平台要求终止前所有窗口已销毁：先按槽序销毁全部存活窗口（含槽表复位），再关闭事件层
        LifeCycle::OnShutdown::destroy_window_pool();
        Event::EventManager::shutdown();
        LifeCycle::OnShutdown::IDWindow_logger_shutdown();

        glfwSetErrorCallback(nullptr);

        // 终止阶段的诊断只统计本阶段产生的错误：先排空此前累计的平台错误，否则旧错误会被误报成终止失败
        (void)glfwGetError(nullptr);
        glfwTerminate();

        if (glfwGetError(nullptr) != GLFW_NO_ERROR)
        {
            // 日志器已销毁：这条只走错误文本通道
            Detail::set_error_text("ID::Window::shutdown: 平台终止阶段上报了错误");
        }

        g_is_initialized.store(false, std::memory_order_relaxed);
    }

    bool is_initialized() noexcept
    {
        return g_is_initialized.load(std::memory_order_relaxed);
    }

    const char* last_error() noexcept
    {
        return g_error_text;
    }

    void poll_events() noexcept
    {
        if (Detail::reject_if_in_callback("ID::Window::poll_events"))
        {
            return;
        }

        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            report_pump_uninitialized("ID::Window::poll_events");
            return;
        }

        glfwPollEvents();
    }

    void wait_events(double timeout_seconds) noexcept
    {
        if (Detail::reject_if_in_callback("ID::Window::wait_events"))
        {
            return;
        }

        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            report_pump_uninitialized("ID::Window::wait_events");
            return;
        }

        // NaN 与 ≤ 0 都落在 !(value > 0.0) 里；∞ 由 isfinite 挡住
        if (!(timeout_seconds > 0.0) || !std::isfinite(timeout_seconds))
        {
            report_invalid_timeout(timeout_seconds);
            return;
        }

        glfwWaitEventsTimeout(timeout_seconds);
    }

    void wake_event_loop() noexcept
    {
        if (Detail::reject_if_in_callback("ID::Window::wake_event_loop"))
        {
            return;
        }

        if (!g_is_initialized.load(std::memory_order_relaxed))
        {
            report_pump_uninitialized("ID::Window::wake_event_loop");
            return;
        }

        glfwPostEmptyEvent();
    }
} // namespace ID::Window

#undef ID_WINDOW_DETAIL_STRINGIZE
#undef ID_WINDOW_DETAIL_STRINGIZE_IMPL
