#include "IDWindowLifecycle.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Kernel/PluginRuntime.hpp"

#include "IDLogApi.hpp"

// GLFW 头只出现在本 TU：插件私有依赖，头目录与宏都不向下游传播。
// `glfw3native.h` 要求 `glfw3.h` 先被包含（它只用、不引入基础声明），因此两步的顺序固定。
// Win32 后端要拿 HWND 才能给图形 API 绑定 surface，因此这里额外暴露原生窗口接口。
#include <GLFW/glfw3.h>

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX    // MinGW 的 os_defines.h 已定义；显式判重避免重定义告警
    #endif
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

/**
 *  ID.Window 插件实现：四阶段接线 + 窗口服务表 + GLFW 私有依赖
 *  1. `on_register` 只发布服务表：不调 `glfwInit`、不建窗口、不碰任何 GLFW 状态；
 *  2. `on_init` 先取 `ID.Log` 的表（依赖已在 plugin.json 声明，拿不到即组装违约，直接返回 false），
 *     再 `glfwInit`：GLFW 的错误统一经 error callback 落进日志，而不是写 stderr；
 *  3. `on_shutdown` 顺序固定：先销毁窗口，再 `glfwTerminate`，最后清空日志表指针；
 *     反序销毁会在已终止的 GLFW 上操作窗口；
 *  4. `on_unload` 只注销自己的槽位；
 *  5. 单窗口策略：产品内只允许一个窗口，句柄由本插件单方持有，`open` 重复调用返回 false。
 *     这是"允许多后端、同时只跑一个"的同一取舍：不做多窗口池、不做句柄表。
 *  6. 本文件不使用异常、RTTI、虚表与 `std::function`；失败一律用 `bool` / 空指针表达，
 *     可恢复失败记 `Level::Warn`/`Error`，不把违约升级成 `Fatal`。
 */

namespace ID::Window
{
    namespace
    {
        // ---- 前向声明：服务表要先引用这些函数，函数体写在表之后 ----
        bool open_entry(const WindowConfig& config) noexcept;
        void close_entry() noexcept;
        bool poll_events_entry() noexcept;
        bool should_close_entry() noexcept;
        void framebuffer_size_entry(int32_t* width, int32_t* height) noexcept;
        void* native_handle_entry() noexcept;

        // ---- 服务表：静态存储期 + constexpr 初始化，地址在进程内不变 ----
        constexpr WindowApiTable g_window_api_table
        {
            .open             = &open_entry,
            .close            = &close_entry,
            .poll_events      = &poll_events_entry,
            .should_close     = &should_close_entry,
            .framebuffer_size = &framebuffer_size_entry,
            .native_handle    = &native_handle_entry,
            .revision         = kWindowApiRevision,
        };

        // 单窗口策略的唯一句柄：常量初始化，零运行期全局构造
        constinit GLFWwindow* g_window            = nullptr;
        constinit bool        g_glfw_initialized  = false;

        // `ID.Log` 的表指针：只在 Ready 区间内有效，`on_shutdown` 清空。
        // 读到的是固定槽位的一次指针 load，不在每次报错时重新取表。
        constinit const Log::LogApiTable* g_log = nullptr;

        inline constexpr char kCategory[] = "ID.Window";

        /**
         *  @brief 冷路径：把一段字节写进 `ID.Log`
         *  @param level `ID::Log::Level` 的数值
         *  @param message 消息；允许为 nullptr（此时不写）
         *  @param len 字节数；不要求 NUL 结尾
         *  @note 只在冷路径调用（初始化、创建窗口、错误回调）；热路径不得打日志。
         *        日志表在 `on_shutdown` 之后为空，因此所有调用点都必须先经本函数的判空。
         */
        void log_message(uint32_t level, const char* message, uint32_t len) noexcept
        {
            if (g_log == nullptr || g_log->write == nullptr || message == nullptr)
            {
                return;
            }
            g_log->write(level, kCategory, static_cast<uint32_t>(sizeof(kCategory) - 1), message, len);
        }

        /**
         *  @brief 冷路径：写一个字符串字面量（长度由数组类型给出，不调用 strlen、不落魔数）
         *  @param level `ID::Log::Level` 的数值
         *  @param text 字面量；末尾 NUL 不计入写入
         */
        template <size_t N>
        void log_literal(uint32_t level, const char (&text)[N]) noexcept
        {
            log_message(level, text, static_cast<uint32_t>(N - 1));
        }

        /**
         *  @brief GLFW 错误回调：把 GLFW 的错误码与描述转成一条日志
         *  @param error_code GLFW 错误码（`GLFW_*` 常量）
         *  @param description GLFW 给出的可读描述；允许为 nullptr
         *  @note 定容栈缓冲 + `snprintf`：不分配、不用 `std::string`；
         *        `snprintf` 的返回值可能超过缓冲容量，因此按容量收敛，只写真实存在的字节。
         */
        void glfw_error_callback(int error_code, const char* description) noexcept
        {
            char buffer[512];
            const int written = std::snprintf(buffer, sizeof(buffer), "GLFW error 0x%X: %s",
                                              static_cast<unsigned>(error_code),
                                              description != nullptr ? description : "(no description)");
            if (written <= 0)
            {
                return;
            }
            uint32_t len = static_cast<uint32_t>(written);
            if (len >= sizeof(buffer))
            {
                len = static_cast<uint32_t>(sizeof(buffer) - 1);
            }
            log_message(static_cast<uint32_t>(Log::Level::Error), buffer, len);
        }

        bool open_entry(const WindowConfig& config) noexcept
        {
            if (g_window != nullptr)
            {
                log_literal(static_cast<uint32_t>(Log::Level::Warn), "open rejected: window already exists");
                return false;
            }

            // 标题在定容数组内兜底 NUL：调用方违约时也不能让 GLFW 读到数组之外
            char title[kMaxTitleBytes];
            std::memcpy(title, config.title, sizeof(title));
            title[kMaxTitleBytes - 1] = '\0';
            const char* const window_title = title[0] != '\0' ? title : "IDGameEngine";

            // 每次都从默认 hint 起算：上一轮的 hint 不得泄漏到下一个窗口
            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_DECORATED, config.decorated ? GLFW_TRUE : GLFW_FALSE);

            const bool wants_context = config.gl_major != 0 || config.gl_minor != 0;
            if (wants_context)
            {
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(config.gl_major));
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(config.gl_minor));
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            }
            else
            {
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            }

            g_window = glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
                                        window_title, nullptr, nullptr);
            if (g_window == nullptr)
            {
                return false;   // 原因已由 error callback 写进日志，本函数不再重复报同一件事
            }

            if (wants_context)
            {
                glfwMakeContextCurrent(g_window);
            }
            return true;
        }

        void close_entry() noexcept
        {
            if (g_window == nullptr)
            {
                return;
            }
            glfwDestroyWindow(g_window);
            g_window = nullptr;
        }

        bool poll_events_entry() noexcept
        {
            if (g_window == nullptr)
            {
                return false;
            }
            glfwPollEvents();
            return glfwWindowShouldClose(g_window) == GLFW_FALSE;
        }

        bool should_close_entry() noexcept
        {
            if (g_window == nullptr)
            {
                return true;   // 没有窗口时"要关闭"是安全默认：调用方不会对着空窗口继续绘制
            }
            return glfwWindowShouldClose(g_window) != GLFW_FALSE;
        }

        void framebuffer_size_entry(int32_t* width, int32_t* height) noexcept
        {
            if (g_window == nullptr)
            {
                return;
            }
            int fb_width  = 0;
            int fb_height = 0;
            glfwGetFramebufferSize(g_window, &fb_width, &fb_height);
            if (width != nullptr)
            {
                *width = static_cast<int32_t>(fb_width);
            }
            if (height != nullptr)
            {
                *height = static_cast<int32_t>(fb_height);
            }
        }

        void* native_handle_entry() noexcept
        {
            #if defined(_WIN32)
                if (g_window == nullptr)
                {
                    return nullptr;
                }
                return static_cast<void*>(glfwGetWin32Window(g_window));
            #else
                return nullptr;   // 只实现 Win32 后端；其它平台不提供伪句柄
            #endif
        }
    } // namespace

    extern "C" void IDWindowOnRegister() noexcept
    {
        // 只发布表：GLFW 的任何初始化都不得出现在本阶段，也不得出现在静态初始化里
        PluginRuntime::instance().publish_service(kWindowSlot, &g_window_api_table);
    }

    extern "C" bool IDWindowOnInit() noexcept
    {
        // 依赖已在 plugin.json 声明：拿不到日志表就是组装违约，不在本插件里静默降级
        g_log = PluginRuntime::instance().get_service<Log::LogApiTable>(ServiceSlot::Log);
        if (g_log == nullptr || g_log->write == nullptr)
        {
            g_log = nullptr;
            return false;
        }

        // 先装 error callback 再 init：init 自身的失败也要经日志报出
        glfwSetErrorCallback(&glfw_error_callback);
        if (glfwInit() == GLFW_FALSE)
        {
            return false;   // 原因已由 error callback 写进日志
        }
        g_glfw_initialized = true;

        // 窗口不在这里创建：`open` 是下游在各自的 `on_init` 序内显式发起的动作，
        // 尺寸、可见性与图形上下文属于产品决策，本插件不替下游选
        log_literal(static_cast<uint32_t>(Log::Level::Info), "ID.Window ready: GLFW initialized");
        return true;
    }

    extern "C" void IDWindowOnShutdown() noexcept
    {
        // 顺序不可交换：窗口必须在 `glfwTerminate` 之前销毁。
        // `on_init` 失败时 runtime 仍会调用本回调（已尝试 init 的前缀包含失败者），
        // 因此两个清理动作都写成幂等空操作
        close_entry();

        if (g_glfw_initialized)
        {
            glfwTerminate();
            g_glfw_initialized = false;
        }

        // 表地址在 shutdown 之后失效：不清空会让下一轮 `on_init` 之前的日志指向已卸载的模块
        g_log = nullptr;
    }

    extern "C" void IDWindowOnUnload() noexcept
    {
        PluginRuntime::instance().revoke_service(kWindowSlot);
    }
} // namespace ID::Window
