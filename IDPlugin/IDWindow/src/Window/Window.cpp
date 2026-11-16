#include "IDWindow/Window.hpp"

#include <cstdint>
#include <limits>
#include <new>
#include <optional>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "DispatchScope.hpp"
#include "IDWindow.hpp"
#include "IDWindow/Event/EventManager.hpp"
#include "IDWindow/KeyCode/KeyCodes.hpp"
#include "IDWindow/WindowPool.hpp"
#include "IDWindowLifeCycle.hpp"
#include "Log/Log.hpp"
#include "Window/GLContextOwner.hpp"

/**
 *  窗口实体实现：平台窗口的创建与销毁、窗口操作与查询、事件派发、平台回调桥、GL 绑定与呈现。
 *
 *  不变量：
 *  1. 本文件是**唯一**读 `WindowDesc::render_desc` 联合体的地方（`open_platform`）；创建完成后能力标志
 *     `m_has_gl_context` 固定。`RenderApi` tag 只在两处读：本文件的 `open_platform`（决定建哪类上下文）
 *     与 `src/Window/Native.cpp` 的 WSI 查询（判断窗口是否为 Vulkan 窗口，不读 `union`）。
 *  2. 平台类型（`GLFWwindow*`、GLFW 常量、Win32 句柄）只出现在 `src/` 的 `.cpp` 里（本文件与
 *     `Native.cpp`）；公开头只有 `void*` 与插件自有类型。
 *  3. 平台回调桥是 `ID::Window::PlatformCallbacks`（本文件定义，公开头只前置声明）：16 个静态函数的
 *     签名逐字对齐 GLFW 3.5.1 的回调 typedef，取回 `Window*` 后转交私有的 `on_platform_event`。
 *     它们是 `Window` 的 friend，因此这条链不需要第三处授权。
 *  4. `on_event` 是本插件唯一按 `EventType` 分派的位置，也是唯一读事件开关的位置；事件只投递给
 *     产生它的那个窗口，不经过池、不按 current 转发。
 *  5. 窗口操作与查询都是一次平台调用、零缓存；失败只写日志与错误文本，编译期常量与异常都不介入。
 *  6. GL 绑定与呈现的归属诊断只经 `Detail::GLContextOps`（`src/Window/GLContextOwner.cpp`）：先校验
 *     真实绑定与 owner，再调驱动；驱动复核失败必须回滚 owner，冲突时不改绑定。
 *  7. 热路径无日志：诊断集中在文件顶部的 `ID_NOINLINE` + `ID_COLD` 冷函数里。
 */
namespace ID::Window
{
    namespace
    {
        constexpr int to_glfw_bool(bool value) noexcept
        {
            return value ? GLFW_TRUE : GLFW_FALSE;
        }

        bool fits_platform_int(std::uint32_t value) noexcept
        {
            return value <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());
        }

        Window* window_of(GLFWwindow* native) noexcept
        {
            if (native == nullptr)
            {
                return nullptr;
            }
            return static_cast<Window*>(glfwGetWindowUserPointer(native));
        }

        // 平台键值 → 已声明键码：域内跳号与未知键值一律丢弃（不构造 KeyCode、不转发事件）
        std::optional<KeyCode> to_key_code(int key) noexcept
        {
            if (key < 0)
            {
                return std::nullopt;
            }

            const KeyCode candidate{ static_cast<IDKeyCodeUnderlyingType>(key) };
            if (!KeyCodes::is_keyboard_key(candidate))
            {
                return std::nullopt;
            }
            return candidate;
        }

        // 平台鼠标键值 → 鼠标键具名集合内的键码：越界值丢弃
        std::optional<KeyCode> to_mouse_button(int button) noexcept
        {
            if (button < 0)
            {
                return std::nullopt;
            }

            const KeyCode candidate{ static_cast<IDKeyCodeUnderlyingType>(button) };
            if (!KeyCodes::is_mouse_button(candidate))
            {
                return std::nullopt;
            }
            return candidate;
        }

        // ---- 上下文查询用的 GL 入口签名与枚举值 ----
        // GL 入口在上下文就绪后用 `glfwGetProcAddress` 取；这里只声明用到的签名与 pname，
        // 不包含任何 GL 头（`GLFW_INCLUDE_NONE`），也不引入 GL loader 依赖

        using GetStringFn   = const unsigned char*(*)(unsigned int);
        using GetIntegervFn = void(*)(unsigned int, int*);

        constexpr unsigned int kGlVersion            = 0x1F02u;     // GL_VERSION
        constexpr unsigned int kGlContextProfileMask = 0x9126u;     // GL_CONTEXT_PROFILE_MASK，GL 3.2+
        constexpr unsigned int kGlContextFlags       = 0x821Eu;     // GL_CONTEXT_FLAGS，GL 4.3+
        constexpr int          kGlCoreProfileBit     = 0x00000001;
        constexpr int          kGlDebugFlagBit       = 0x00000002;

        /**
         *  GL 版本读数：从 `GL_VERSION` 串的前两段数字解析，不分配、不格式化
         */
        struct GLVersionReading
        {
            std::uint32_t   major    = 0;
            std::uint32_t   minor    = 0;
            bool            is_valid = false;
        };

        GLVersionReading parse_gl_version(const char* text) noexcept
        {
            GLVersionReading reading;
            if (text == nullptr)
            {
                return reading;
            }

            const char* cursor = text;
            while (*cursor == ' ')
            {
                ++cursor;
            }

            while (*cursor >= '0' && *cursor <= '9')
            {
                reading.major = reading.major * 10 + static_cast<std::uint32_t>(*cursor - '0');
                ++cursor;
            }

            if (*cursor != '.')
            {
                return reading;
            }
            ++cursor;

            while (*cursor >= '0' && *cursor <= '9')
            {
                reading.minor = reading.minor * 10 + static_cast<std::uint32_t>(*cursor - '0');
                ++cursor;
            }

            reading.is_valid = (reading.major > 0);
            return reading;
        }

        // ---- 失败冷路径：不内联、集中放置，热路径上没有日志 ----

        ID_NOINLINE ID_COLD void report_uninitialized(const char* function_name) noexcept
        {
            Detail::set_error_text("窗口子系统尚未初始化");
            IDWINDOW_LOG_ERROR("{}: 窗口子系统尚未初始化，本次调用被忽略", function_name);
        }

        ID_NOINLINE ID_COLD void report_platform_missing(const char* function_name) noexcept
        {
            Detail::set_error_text("平台窗口不存在：尚未创建或已销毁");
            IDWINDOW_LOG_ERROR("{}: 平台窗口不存在（尚未创建或已销毁），本次调用被忽略", function_name);
        }

        ID_NOINLINE ID_COLD void report_empty_title(const char* function_name) noexcept
        {
            Detail::set_error_text("标题为空：nullptr 或空串");
            IDWINDOW_LOG_WARN("{}: 标题为 nullptr 或空串，本次调用被忽略", function_name);
        }

        ID_NOINLINE ID_COLD void report_empty_name(const char* function_name) noexcept
        {
            Detail::set_error_text("入口名为空");
            IDWINDOW_LOG_WARN("{}: 入口名为 nullptr 或空串，返回 nullptr", function_name);
        }

        ID_NOINLINE ID_COLD void report_invalid_size(const char* function_name, std::uint32_t width, std::uint32_t height) noexcept
        {
            Detail::set_error_text("尺寸非法：宽高必须大于 0 且可被平台整数表达");
            IDWINDOW_LOG_WARN("{}: 尺寸 {}x{} 非法（必须大于 0 且可被平台整数表达），本次调用被忽略", function_name, width, height);
        }

        ID_NOINLINE ID_COLD void report_not_gl_window(const char* function_name) noexcept
        {
            Detail::set_error_text("该窗口没有 GL 上下文（RenderApi 不是 OpenGL）");
            IDWINDOW_LOG_WARN("{}: 该窗口不是 GL 上下文窗口，本次调用被忽略", function_name);
        }

        ID_NOINLINE ID_COLD void report_no_current_context(const char* function_name) noexcept
        {
            Detail::set_error_text("调用线程没有 current GL 上下文");
            IDWINDOW_LOG_WARN("{}: 调用线程没有 current GL 上下文，本次调用被忽略", function_name);
        }

        ID_NOINLINE ID_COLD void report_invalid_key(const char* function_name, std::uint16_t key) noexcept
        {
            Detail::set_error_text("键码不在已声明集合内");
            IDWINDOW_LOG_WARN("{}: 键码 {} 不在已声明集合内（域内跳号或未知键值），返回 false", function_name, key);
        }

        ID_NOINLINE ID_COLD void report_gl_query_failed(const char* function_name) noexcept
        {
            Detail::set_error_text("GL 入口查询失败：取不到该上下文的版本信息");
            IDWINDOW_LOG_ERROR("{}: 取不到该上下文的 GL 版本信息（入口地址或返回值不可用），返回零值", function_name);
        }

        ID_NOINLINE ID_COLD void report_context_not_current(const char* function_name) noexcept
        {
            Detail::set_error_text("本线程 current 的 GL 上下文不是该窗口的上下文");
            IDWINDOW_LOG_WARN("{}: 本线程 current 的 GL 上下文不是该窗口的上下文，本次调用被忽略", function_name);
        }

        ID_NOINLINE ID_COLD void report_context_owner_conflict(const char* function_name) noexcept
        {
            Detail::set_error_text("该 GL 上下文已被另一线程持有：先 release_context 再交接");
            IDWINDOW_LOG_ERROR("{}: 该 GL 上下文已被另一线程持有，本次调用被拒绝且未改变绑定与 owner", function_name);
        }

        ID_NOINLINE ID_COLD void report_context_bind_failed(const char* function_name) noexcept
        {
            Detail::set_error_text("平台的上下文绑定调用没有生效：owner 已回滚");
            IDWINDOW_LOG_ERROR("{}: 平台的上下文绑定调用没有生效，owner 已回滚", function_name);
        }

        ID_NOINLINE ID_COLD void report_share_source_invalid(const char* function_name) noexcept
        {
            Detail::set_error_text("共享源窗口没有 GL 上下文：share_with 必须指向存活的 OpenGL 窗口");
            IDWINDOW_LOG_ERROR("{}: 共享源窗口没有 GL 上下文，创建共享上下文被拒绝", function_name);
        }

        ID_NOINLINE ID_COLD void report_share_source_current(const char* function_name) noexcept
        {
            Detail::set_error_text("共享源的上下文仍处于绑定状态：先 release_context 再创建共享上下文");
            IDWINDOW_LOG_ERROR("{}: 共享源的上下文仍处于绑定状态，创建共享上下文被拒绝", function_name);
        }

        ID_NOINLINE ID_COLD void report_lose_focus_unsupported() noexcept
        {
            Detail::set_error_text("平台没有放弃焦点的调用，lose_focus 按空操作处理");
            IDWINDOW_LOG_WARN("Window::lose_focus: 平台没有\"放弃焦点\"的调用，按空操作处理");
        }

        ID_NOINLINE ID_COLD void report_invalid_gl_desc(const char* function_name) noexcept
        {
            Detail::set_error_text("OpenGL 上下文描述的数值超出平台整数表达范围");
            IDWINDOW_LOG_ERROR("{}: OpenGL 上下文描述中有数值超出平台整数表达范围，创建被拒绝", function_name);
        }

        ID_NOINLINE ID_COLD void report_creation_failed(const char* function_name) noexcept
        {
            Detail::set_error_text("平台窗口创建失败：上下文或窗口属性无法满足");
            IDWINDOW_LOG_ERROR("{}: 平台窗口创建失败（上下文或窗口属性无法满足）", function_name);
        }
    } // namespace

    /**
     *  平台回调桥：签名逐字对齐 GLFW 3.5.1 的回调 typedef，只负责取回 `Window*`、组事件载荷并转交
     *  `Window::on_platform_event`。它不是公开接口：公开头只前置声明，消费者看不到成员也无法调用。
     */
    struct PlatformCallbacks
    {
        static void on_key(GLFWwindow* native, int key, int /*scancode*/, int action, int /*mods*/) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const std::optional<KeyCode> key_code = to_key_code(key);
            if (!key_code.has_value())
            {
                return;
            }

            switch (action)
            {
                case GLFW_PRESS:
                    window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::KeyPressed>{ *key_code } });
                    return;
                case GLFW_RELEASE:
                    window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::KeyReleased>{ *key_code } });
                    return;
                case GLFW_REPEAT:
                    window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::KeyRepeat>{ *key_code } });
                    return;
                default:
                    return;
            }
        }

        static void on_mouse_button(GLFWwindow* native, int button, int action, int /*mods*/) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const std::optional<KeyCode> key_code = to_mouse_button(button);
            if (!key_code.has_value())
            {
                return;
            }

            switch (action)
            {
                case GLFW_PRESS:
                    window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::MouseButtonPressed>{ *key_code } });
                    return;
                case GLFW_RELEASE:
                    window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::MouseButtonReleased>{ *key_code } });
                    return;
                default:
                    return;
            }
        }

        static void on_cursor_pos(GLFWwindow* native, double xpos, double ypos) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const Mouse::Position position{ xpos, ypos };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::MouseMoved>{ position } });
        }

        static void on_scroll(GLFWwindow* native, double xoffset, double yoffset) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const Mouse::ScrollOffset offset{ xoffset, yoffset };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::MouseScrolled>{ offset } });
        }

        static void on_char(GLFWwindow* native, unsigned int codepoint) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            // 入站校验：超出 Unicode scalar 范围或落在 surrogate 区间的字符丢弃，不转发 TextInput
            if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
            {
                return;
            }

            const Event::Codepoint value{ static_cast<std::uint32_t>(codepoint) };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::TextInput>{ value } });
        }

        static void on_window_close(GLFWwindow* native) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowClose>{} });
        }

        static void on_window_size(GLFWwindow* native, int width, int height) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const WindowSize size{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowResize>{ size } });
        }

        static void on_framebuffer_size(GLFWwindow* native, int width, int height) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const WindowSize size{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowFramebufferResize>{ size } });
        }

        static void on_window_pos(GLFWwindow* native, int xpos, int ypos) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const WindowPosition position{ xpos, ypos };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowMoved>{ position } });
        }

        static void on_window_focus(GLFWwindow* native, int focused) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            if (focused == GLFW_TRUE)
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowFocus>{} });
            }
            else
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowLostFocus>{} });
            }
        }

        static void on_window_iconify(GLFWwindow* native, int iconified) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            if (iconified == GLFW_TRUE)
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowMinimized>{} });
            }
            else
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowRestored>{} });
            }
        }

        static void on_window_maximize(GLFWwindow* native, int maximized) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            if (maximized == GLFW_TRUE)
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowMaximized>{} });
            }
            else
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowUnmaximized>{} });
            }
        }

        static void on_window_refresh(GLFWwindow* native) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowRefresh>{} });
        }

        static void on_cursor_enter(GLFWwindow* native, int entered) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            if (entered == GLFW_TRUE)
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowCursorEntered>{} });
            }
            else
            {
                window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowCursorLeft>{} });
            }
        }

        static void on_file_drop(GLFWwindow* native, int path_count, const char* paths[]) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const Event::FileDropList drops{ paths, static_cast<std::uint32_t>(path_count) };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::FileDrop>{ drops } });
        }

        static void on_content_scale(GLFWwindow* native, float xscale, float yscale) noexcept
        {
            Window* const window = window_of(native);
            if (window == nullptr)
            {
                return;
            }

            const WindowScale scale{ xscale, yscale };
            window->on_platform_event(Event::EventContext{ Event::Event<Event::EventType::WindowContentScaleChanged>{ scale } });
        }
    };

    Window::Window(const WindowDesc& desc, WindowHandle handle) noexcept
        : m_native_window(nullptr)
        , m_handle(handle)
        , m_desc(desc)
        , m_has_gl_context(false)
    {
    }

    Window::~Window()
    {
        destroy();      // 幂等：池已显式调用过 destroy() 时这里是无操作
    }

    bool Window::open_platform() noexcept
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_RESIZABLE, to_glfw_bool(m_desc.is_resizable));
        glfwWindowHint(GLFW_DECORATED, to_glfw_bool(m_desc.is_decorated));
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);       // 先隐藏创建，成功后再按 is_visible 显示

        GLFWwindow* shared = nullptr;

        if (m_desc.render_api == RenderApi::OpenGL)
        {
            const OpenGLContextDesc& opengl = m_desc.render_desc.opengl;

            // 位宽与采样数是 softPreferences，只做"能否被平台整数表达"的输入校验，不做钳制
            if (!fits_platform_int(opengl.major) || !fits_platform_int(opengl.minor)
                || !fits_platform_int(opengl.framebuffer.red_bits) || !fits_platform_int(opengl.framebuffer.green_bits)
                || !fits_platform_int(opengl.framebuffer.blue_bits) || !fits_platform_int(opengl.framebuffer.alpha_bits)
                || !fits_platform_int(opengl.framebuffer.depth_bits) || !fits_platform_int(opengl.framebuffer.stencil_bits)
                || !fits_platform_int(opengl.framebuffer.samples))
            {
                report_invalid_gl_desc("Window::open_platform");
                return false;
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

            // 硬约束：版本、profile、向前兼容、双缓冲；平台无法满足时 glfwCreateWindow 直接失败
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(opengl.major));
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(opengl.minor));
            glfwWindowHint(GLFW_OPENGL_PROFILE, opengl.profile == GLProfile::Core
                ? GLFW_OPENGL_CORE_PROFILE
                : GLFW_OPENGL_COMPAT_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, to_glfw_bool(opengl.is_forward_compatible));
            glfwWindowHint(GLFW_DOUBLEBUFFER, to_glfw_bool(opengl.is_double_buffered));

            // 请求偏好：只请求 debug 上下文，拿不到也照样创建
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, to_glfw_bool(opengl.is_debug));
            IDWINDOW_LOG_DEBUG("Window::open_platform: 传入的 is_debug 为 {}", opengl.is_debug);

            // softPreferences：只下发 hint，实际值由 renderer 在上下文就绪后用 GL 查询
            glfwWindowHint(GLFW_RED_BITS, static_cast<int>(opengl.framebuffer.red_bits));
            glfwWindowHint(GLFW_GREEN_BITS, static_cast<int>(opengl.framebuffer.green_bits));
            glfwWindowHint(GLFW_BLUE_BITS, static_cast<int>(opengl.framebuffer.blue_bits));
            glfwWindowHint(GLFW_ALPHA_BITS, static_cast<int>(opengl.framebuffer.alpha_bits));
            glfwWindowHint(GLFW_DEPTH_BITS, static_cast<int>(opengl.framebuffer.depth_bits));
            glfwWindowHint(GLFW_STENCIL_BITS, static_cast<int>(opengl.framebuffer.stencil_bits));
            glfwWindowHint(GLFW_SAMPLES, static_cast<int>(opengl.framebuffer.samples));
            glfwWindowHint(GLFW_SRGB_CAPABLE, to_glfw_bool(opengl.framebuffer.is_srgb));

            if (opengl.share_with.is_valid())
            {
                // 无效句柄表示不共享；有效句柄必须指向存活的 GL 上下文窗口
                // （同一类的成员函数可以读其它实例的私有成员，因此不需要额外授权）
                Window& source = WindowPool::get_window(opengl.share_with);
                if (source.m_native_window == nullptr || !source.m_has_gl_context)
                {
                    report_share_source_invalid("Window::open_platform");
                    return false;
                }

                // 共享源必须已同步且未 current：本插件统一收紧为跨平台前提。"已同步"由应用负责，
                // 插件能诊断的是"未 current"——本线程的真实绑定或 owner 指向它都算仍在使用
                void* const source_context = source.m_native_window;
                if (Detail::GLContextOps::get_bound_context() == source_context
                    || Detail::GLContextOps::get_owner() == source_context)
                {
                    report_share_source_current("Window::open_platform");
                    return false;
                }

                shared = static_cast<GLFWwindow*>(source.m_native_window);
            }
        }
        else
        {
            // RenderApi::None 与 RenderApi::Vulkan 都不建 GL 上下文
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        GLFWwindow* const native = glfwCreateWindow(static_cast<int>(m_desc.size.width),
                                                   static_cast<int>(m_desc.size.height),
                                                   m_desc.title,
                                                   nullptr,
                                                   shared);
        if (native == nullptr)
        {
            report_creation_failed("Window::open_platform");
            return false;
        }

        m_native_window  = native;
        m_has_gl_context = (m_desc.render_api == RenderApi::OpenGL);

        if (m_has_gl_context)
        {
            // 软偏好：请求值与实际值不同只记录、不判失败（硬约束由 glfwCreateWindow 在创建期把关）
            const bool is_debug = glfwGetWindowAttrib(native, GLFW_OPENGL_DEBUG_CONTEXT) == GLFW_TRUE;
            IDWINDOW_LOG_DEBUG("Window::open_platform: 经过上下文创建后，is_debug 为 {}", is_debug);
        }

        glfwSetWindowUserPointer(native, this);

        glfwSetKeyCallback(native, PlatformCallbacks::on_key);
        glfwSetMouseButtonCallback(native, PlatformCallbacks::on_mouse_button);
        glfwSetCursorPosCallback(native, PlatformCallbacks::on_cursor_pos);
        glfwSetCharCallback(native, PlatformCallbacks::on_char);
        glfwSetWindowCloseCallback(native, PlatformCallbacks::on_window_close);
        glfwSetWindowSizeCallback(native, PlatformCallbacks::on_window_size);
        glfwSetFramebufferSizeCallback(native, PlatformCallbacks::on_framebuffer_size);
        glfwSetWindowFocusCallback(native, PlatformCallbacks::on_window_focus);
        glfwSetWindowIconifyCallback(native, PlatformCallbacks::on_window_iconify);
        glfwSetWindowMaximizeCallback(native, PlatformCallbacks::on_window_maximize);
        glfwSetWindowRefreshCallback(native, PlatformCallbacks::on_window_refresh);
        glfwSetCursorEnterCallback(native, PlatformCallbacks::on_cursor_enter);
        glfwSetScrollCallback(native, PlatformCallbacks::on_scroll);
        glfwSetDropCallback(native, PlatformCallbacks::on_file_drop);
        glfwSetWindowPosCallback(native, PlatformCallbacks::on_window_pos);
        glfwSetWindowContentScaleCallback(native, PlatformCallbacks::on_content_scale);

        if (m_desc.is_visible)
        {
            glfwShowWindow(native);
        }

        IDWINDOW_LOG_INFO("Window::open_platform: 平台窗口已创建，句柄 {}", m_handle.get_id());
        return true;
    }

    void Window::destroy() noexcept
    {
        if (m_native_window == nullptr)
        {
            return;
        }

        GLFWwindow* const native = static_cast<GLFWwindow*>(m_native_window);

        // 上下文随窗口一起失效：owner 不得继续指向它，否则地址被复用后新上下文会被误判为他人持有
        if (m_has_gl_context)
        {
            Detail::GLContextOps::forget_owner(native);
        }

        // 先注销平台回调，再销毁窗口：销毁过程中不再进入本窗口的任何回调
        glfwSetKeyCallback(native, nullptr);
        glfwSetMouseButtonCallback(native, nullptr);
        glfwSetCursorPosCallback(native, nullptr);
        glfwSetCharCallback(native, nullptr);
        glfwSetWindowCloseCallback(native, nullptr);
        glfwSetWindowSizeCallback(native, nullptr);
        glfwSetFramebufferSizeCallback(native, nullptr);
        glfwSetWindowFocusCallback(native, nullptr);
        glfwSetWindowIconifyCallback(native, nullptr);
        glfwSetWindowMaximizeCallback(native, nullptr);
        glfwSetWindowRefreshCallback(native, nullptr);
        glfwSetCursorEnterCallback(native, nullptr);
        glfwSetScrollCallback(native, nullptr);
        glfwSetDropCallback(native, nullptr);
        glfwSetWindowPosCallback(native, nullptr);
        glfwSetWindowContentScaleCallback(native, nullptr);
        glfwSetWindowUserPointer(native, nullptr);

        glfwDestroyWindow(native);

        m_native_window  = nullptr;
        m_has_gl_context = false;

        IDWINDOW_LOG_INFO("Window::destroy: 平台窗口已销毁，句柄 {}", m_handle.get_id());
    }

    void Window::on_platform_event(const Event::EventContext& event) noexcept
    {
        on_event(event);
    }

    void Window::on_event(const Event::EventContext& event) noexcept
    {
        // 第一步：事件开关。这是全插件唯一的开关判定点，关闭时静默丢弃：不缓存、不计数、无第二条路径
        if (!Event::EventManager::is_enabled())
        {
            return;
        }

        // 第二步：派发深度。进入加一、返回减一；深度只服务回调栈内的违约判定，不参与转发判定
        const Detail::DispatchScope scope;

        const Event::EventType type = event.type;

        // 哨兵与无效 tag 不进下标：None 表示没有事件，Count 与更大值都不是合法下标
        if (type == Event::EventType::None
            || Event::to_event_type_uint(type) >= Event::to_event_type_uint(Event::EventType::Count))
        {
            return;
        }

        // 第三步：按 tag 读该事件类型自己的槽位、构造 POD 后调用；对应成员为 nullptr 即该组未注册
        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(type)];

        switch (type)
        {
            case Event::EventType::None:
            case Event::EventType::Count:
            {
                // 上面已挡住这两个哨兵；保留分支只为让 switch 覆盖全部枚举值（新增事件类型时由 -Wswitch 提示）
                return;
            }

            case Event::EventType::WindowClose:
            {
                if (entry.fn.window_close == nullptr)
                {
                    return;
                }

                entry.fn.window_close(entry.user, *this, Event::WindowCloseEvent{});
                return;
            }

            case Event::EventType::WindowResize:
            {
                if (entry.fn.window_size == nullptr)
                {
                    return;
                }

                const Event::WindowSizeEvent payload{ event.to_event<Event::EventType::WindowResize>().payload };
                entry.fn.window_size(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowFocus:
            {
                if (entry.fn.window_focus == nullptr)
                {
                    return;
                }

                const Event::WindowFocusEvent payload{ true };
                entry.fn.window_focus(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowLostFocus:
            {
                if (entry.fn.window_focus == nullptr)
                {
                    return;
                }

                const Event::WindowFocusEvent payload{ false };
                entry.fn.window_focus(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowMoved:
            {
                if (entry.fn.window_pos == nullptr)
                {
                    return;
                }

                const Event::WindowPosEvent payload{ event.to_event<Event::EventType::WindowMoved>().payload };
                entry.fn.window_pos(entry.user, *this, payload);
                return;
            }

            case Event::EventType::KeyPressed:
            {
                if (entry.fn.key == nullptr)
                {
                    return;
                }

                const Event::KeyEvent payload{ event.to_event<Event::EventType::KeyPressed>().payload,
                                               Event::KeyAction::Pressed };
                entry.fn.key(entry.user, *this, payload);
                return;
            }

            case Event::EventType::KeyReleased:
            {
                if (entry.fn.key == nullptr)
                {
                    return;
                }

                const Event::KeyEvent payload{ event.to_event<Event::EventType::KeyReleased>().payload,
                                               Event::KeyAction::Released };
                entry.fn.key(entry.user, *this, payload);
                return;
            }

            case Event::EventType::KeyRepeat:
            {
                if (entry.fn.key == nullptr)
                {
                    return;
                }

                const Event::KeyEvent payload{ event.to_event<Event::EventType::KeyRepeat>().payload,
                                               Event::KeyAction::Repeat };
                entry.fn.key(entry.user, *this, payload);
                return;
            }

            case Event::EventType::MouseButtonPressed:
            {
                if (entry.fn.mouse_button == nullptr)
                {
                    return;
                }

                const Event::MouseButtonEvent payload{ event.to_event<Event::EventType::MouseButtonPressed>().payload,
                                                       Event::KeyAction::Pressed };
                entry.fn.mouse_button(entry.user, *this, payload);
                return;
            }

            case Event::EventType::MouseButtonReleased:
            {
                if (entry.fn.mouse_button == nullptr)
                {
                    return;
                }

                const Event::MouseButtonEvent payload{ event.to_event<Event::EventType::MouseButtonReleased>().payload,
                                                       Event::KeyAction::Released };
                entry.fn.mouse_button(entry.user, *this, payload);
                return;
            }

            case Event::EventType::MouseMoved:
            {
                if (entry.fn.cursor_pos == nullptr)
                {
                    return;
                }

                const Event::CursorPosEvent payload{ event.to_event<Event::EventType::MouseMoved>().payload };
                entry.fn.cursor_pos(entry.user, *this, payload);
                return;
            }

            case Event::EventType::MouseScrolled:
            {
                if (entry.fn.scroll == nullptr)
                {
                    return;
                }

                const Event::ScrollEvent payload{ event.to_event<Event::EventType::MouseScrolled>().payload };
                entry.fn.scroll(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowFramebufferResize:
            {
                if (entry.fn.framebuffer_size == nullptr)
                {
                    return;
                }

                const Event::WindowSizeEvent payload{ event.to_event<Event::EventType::WindowFramebufferResize>().payload };
                entry.fn.framebuffer_size(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowContentScaleChanged:
            {
                if (entry.fn.content_scale == nullptr)
                {
                    return;
                }

                const Event::ContentScaleEvent payload{ event.to_event<Event::EventType::WindowContentScaleChanged>().payload };
                entry.fn.content_scale(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowMinimized:
            {
                if (entry.fn.iconify == nullptr)
                {
                    return;
                }

                const Event::WindowIconifyEvent payload{ true };
                entry.fn.iconify(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowRestored:
            {
                if (entry.fn.iconify == nullptr)
                {
                    return;
                }

                const Event::WindowIconifyEvent payload{ false };
                entry.fn.iconify(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowMaximized:
            {
                if (entry.fn.maximize == nullptr)
                {
                    return;
                }

                const Event::WindowMaximizeEvent payload{ true };
                entry.fn.maximize(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowUnmaximized:
            {
                if (entry.fn.maximize == nullptr)
                {
                    return;
                }

                const Event::WindowMaximizeEvent payload{ false };
                entry.fn.maximize(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowRefresh:
            {
                if (entry.fn.refresh == nullptr)
                {
                    return;
                }

                entry.fn.refresh(entry.user, *this, Event::WindowRefreshEvent{});
                return;
            }

            case Event::EventType::WindowCursorEntered:
            {
                if (entry.fn.cursor_enter == nullptr)
                {
                    return;
                }

                const Event::CursorEnterEvent payload{ true };
                entry.fn.cursor_enter(entry.user, *this, payload);
                return;
            }

            case Event::EventType::WindowCursorLeft:
            {
                if (entry.fn.cursor_enter == nullptr)
                {
                    return;
                }

                const Event::CursorEnterEvent payload{ false };
                entry.fn.cursor_enter(entry.user, *this, payload);
                return;
            }

            case Event::EventType::TextInput:
            {
                if (entry.fn.char_input == nullptr)
                {
                    return;
                }

                const Event::CharEvent payload{ event.to_event<Event::EventType::TextInput>().payload };
                entry.fn.char_input(entry.user, *this, payload);
                return;
            }

            case Event::EventType::FileDrop:
            {
                if (entry.fn.file_drop == nullptr)
                {
                    return;
                }

                const Event::FileDropEvent payload{ event.to_event<Event::EventType::FileDrop>().payload };
                entry.fn.file_drop(entry.user, *this, payload);
                return;
            }
        }
    }

    // ========== 窗口操作 ==========

    void Window::set_title(const char* utf8_title) noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::set_title");
            return;
        }

        if (utf8_title == nullptr || utf8_title[0] == '\0')
        {
            report_empty_title("Window::set_title");
            return;
        }

        glfwSetWindowTitle(static_cast<GLFWwindow*>(m_native_window), utf8_title);
    }

    void Window::set_size(const WindowSize& size) noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::set_size");
            return;
        }

        if (size.width == 0 || size.height == 0 || !fits_platform_int(size.width) || !fits_platform_int(size.height))
        {
            report_invalid_size("Window::set_size", size.width, size.height);
            return;
        }

        glfwSetWindowSize(static_cast<GLFWwindow*>(m_native_window),
                          static_cast<int>(size.width),
                          static_cast<int>(size.height));
    }

    void Window::show() noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::show");
            return;
        }

        glfwShowWindow(static_cast<GLFWwindow*>(m_native_window));
    }

    void Window::hide() noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::hide");
            return;
        }

        glfwHideWindow(static_cast<GLFWwindow*>(m_native_window));
    }

    void Window::focus() noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::focus");
            return;
        }

        glfwFocusWindow(static_cast<GLFWwindow*>(m_native_window));
    }

    void Window::lose_focus() noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::lose_focus");
            return;
        }

        report_lose_focus_unsupported();
    }

    void Window::request_close() noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::request_close");
            return;
        }

        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(m_native_window), GLFW_TRUE);
    }

    void Window::cancel_close() noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::cancel_close");
            return;
        }

        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(m_native_window), GLFW_FALSE);
    }

    // ========== 状态查询 ==========

    bool Window::should_close() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::should_close");
            return false;
        }

        return glfwWindowShouldClose(static_cast<GLFWwindow*>(m_native_window)) == GLFW_TRUE;
    }

    bool Window::is_visible() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::is_visible");
            return false;
        }

        return glfwGetWindowAttrib(static_cast<GLFWwindow*>(m_native_window), GLFW_VISIBLE) == GLFW_TRUE;
    }

    bool Window::is_focused() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::is_focused");
            return false;
        }

        return glfwGetWindowAttrib(static_cast<GLFWwindow*>(m_native_window), GLFW_FOCUSED) == GLFW_TRUE;
    }

    WindowSize Window::get_size() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_size");
            return WindowSize{};
        }

        int width  = 0;
        int height = 0;
        glfwGetWindowSize(static_cast<GLFWwindow*>(m_native_window), &width, &height);

        return WindowSize{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
    }

    Window::FramebufferSize Window::get_framebuffer_size() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_framebuffer_size");
            return FramebufferSize{};
        }

        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_native_window), &width, &height);

        return FramebufferSize{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
    }

    WindowScale Window::get_content_scale() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_content_scale");
            return WindowScale{};
        }

        float xscale = 1.0f;
        float yscale = 1.0f;
        glfwGetWindowContentScale(static_cast<GLFWwindow*>(m_native_window), &xscale, &yscale);

        return WindowScale{ xscale, yscale };
    }

    const char* Window::get_title() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_title");
            return nullptr;
        }

        return glfwGetWindowTitle(static_cast<GLFWwindow*>(m_native_window));
    }

    WindowStatus Window::get_status() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_status");
            return WindowStatus{};
        }

        GLFWwindow* const native = static_cast<GLFWwindow*>(m_native_window);

        // 现场组合六个属性位与关闭请求标志：不缓存，也不保留上一轮的状态
        IDWindowStatusUINT bits = 0;

        if (glfwGetWindowAttrib(native, GLFW_VISIBLE) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::Visible);
        }
        if (glfwGetWindowAttrib(native, GLFW_FOCUSED) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::Focused);
        }
        if (glfwGetWindowAttrib(native, GLFW_ICONIFIED) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::Iconified);
        }
        if (glfwGetWindowAttrib(native, GLFW_MAXIMIZED) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::Maximized);
        }
        if (glfwGetWindowAttrib(native, GLFW_RESIZABLE) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::Resizable);
        }
        if (glfwGetWindowAttrib(native, GLFW_DECORATED) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::Decorated);
        }
        if (glfwWindowShouldClose(native) == GLFW_TRUE)
        {
            bits |= static_cast<IDWindowStatusUINT>(WindowStatusEnum::ShouldClose);
        }

        return WindowStatus{ bits };
    }

    bool Window::get_key_state(KeyCode key) const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_key_state");
            return false;
        }

        if (!KeyCodes::is_keyboard_key(key))
        {
            report_invalid_key("Window::get_key_state", key.get_key());
            return false;
        }

        return glfwGetKey(static_cast<GLFWwindow*>(m_native_window), static_cast<int>(key.get_key())) == GLFW_PRESS;
    }

    bool Window::get_mouse_button_state(KeyCode button) const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_mouse_button_state");
            return false;
        }

        if (!KeyCodes::is_mouse_button(button))
        {
            report_invalid_key("Window::get_mouse_button_state", button.get_key());
            return false;
        }

        return glfwGetMouseButton(static_cast<GLFWwindow*>(m_native_window), static_cast<int>(button.get_key())) == GLFW_PRESS;
    }

    Mouse::Position Window::get_mouse_position() const noexcept
    {
        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_mouse_position");
            return Mouse::Position{};
        }

        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(static_cast<GLFWwindow*>(m_native_window), &xpos, &ypos);

        return Mouse::Position{ xpos, ypos };
    }

    WindowHandle Window::get_handle() const noexcept
    {
        return m_handle;
    }

    // ========== GL 与呈现 ==========

    void Window::swap_buffers() noexcept
    {
        if (Detail::reject_if_in_callback("Window::swap_buffers"))
        {
            return;
        }

        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::swap_buffers");
            return;
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::swap_buffers");
            return;
        }

        if (!m_has_gl_context)
        {
            report_not_gl_window("Window::swap_buffers");
            return;
        }

        // 呈现要求本线程 current 的就是该窗口自己的上下文：不匹配时驱动会呈现别的后备缓冲
        if (Detail::GLContextOps::get_bound_context() != m_native_window)
        {
            report_context_not_current("Window::swap_buffers");
            return;
        }

        glfwSwapBuffers(static_cast<GLFWwindow*>(m_native_window));
    }

    void Window::make_context_current() noexcept
    {
        if (Detail::reject_if_in_callback("Window::make_context_current"))
        {
            return;
        }

        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::make_context_current");
            return;
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::make_context_current");
            return;
        }

        if (!m_has_gl_context)
        {
            report_not_gl_window("Window::make_context_current");
            return;
        }

        void* const context = m_native_window;

        // 同线程同上下文：短路。不调驱动、不改 owner，只在本线程已由平台绑定成功时补记无主的 owner
        if (Detail::GLContextOps::get_bound_context() == context)
        {
            Detail::GLContextOps::adopt_owner(context);
            return;
        }

        // 先用 CAS 尝试把归属转给本线程：同一上下文被另一线程持有时在此被拒绝，绑定与 owner 都不改
        const Detail::GLContextClaim claim = Detail::GLContextOps::claim_owner(context);
        if (claim.is_conflict)
        {
            report_context_owner_conflict("Window::make_context_current");
            return;
        }

        // 再以驱动的真实绑定复核为准：平台没有生效时必须回滚 owner，不把缓存当结论
        if (Detail::GLContextOps::bind_context(context))
        {
            return;
        }

        Detail::GLContextOps::rollback_owner(context, claim);
        report_context_bind_failed("Window::make_context_current");
    }

    void Window::release_context() noexcept
    {
        if (Detail::reject_if_in_callback("Window::release_context"))
        {
            return;
        }

        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::release_context");
            return;
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::release_context");
            return;
        }

        // 只清本线程的绑定：本线程本来没有 current 就算成功（幂等），其它线程的绑定不受影响
        void* const unbound = Detail::GLContextOps::get_bound_context();
        if (unbound == nullptr)
        {
            // 本线程没有绑定：顺手清掉本线程留下、仍指向该窗口的 owner（诊断自愈；别的线程持有则不动）
            Detail::GLContextOps::release_owner_of(m_native_window);
            return;
        }

        if (!Detail::GLContextOps::bind_context(nullptr))
        {
            report_context_bind_failed("Window::release_context");
            return;
        }

        Detail::GLContextOps::release_owner_of(unbound);
    }

    void Window::set_vsync(bool enabled) noexcept
    {
        // 回调栈内允许（实施计划书 §7.5）：前提是该 context 归本线程 current，由下面的绑定校验把关
        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::set_vsync");
            return;
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::set_vsync");
            return;
        }

        if (!m_has_gl_context)
        {
            report_not_gl_window("Window::set_vsync");
            return;
        }

        void* const bound = Detail::GLContextOps::get_bound_context();
        if (bound == nullptr)
        {
            report_no_current_context("Window::set_vsync");
            return;
        }

        // 只作用于当前 context：本线程 current 的不是该窗口时不得改其它上下文的交换间隔
        if (bound != m_native_window)
        {
            report_context_not_current("Window::set_vsync");
            return;
        }

        glfwSwapInterval(enabled ? 1 : 0);
    }

    ProcAddress Window::get_proc_address(const char* name) noexcept
    {
        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::get_proc_address");
            return nullptr;
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_proc_address");
            return nullptr;
        }

        if (name == nullptr || name[0] == '\0')
        {
            report_empty_name("Window::get_proc_address");
            return nullptr;
        }

        if (!m_has_gl_context)
        {
            report_not_gl_window("Window::get_proc_address");
            return nullptr;
        }

        void* const bound = Detail::GLContextOps::get_bound_context();
        if (bound == nullptr)
        {
            report_no_current_context("Window::get_proc_address");
            return nullptr;
        }

        // 必须在正确的 current 上下文上查询：不假设不同上下文返回同一地址
        if (bound != m_native_window)
        {
            report_context_not_current("Window::get_proc_address");
            return nullptr;
        }

        // 查不到某个入口是扩展探测的常规结果，不是失败：返回 nullptr 且不写日志
        return glfwGetProcAddress(name);
    }

    ContextInfo Window::get_context_info() const noexcept
    {
        if (!ID::Window::is_initialized())
        {
            report_uninitialized("Window::get_context_info");
            return ContextInfo{};
        }

        if (m_native_window == nullptr)
        {
            report_platform_missing("Window::get_context_info");
            return ContextInfo{};
        }

        if (!m_has_gl_context)
        {
            report_not_gl_window("Window::get_context_info");
            return ContextInfo{};
        }

        void* const bound = Detail::GLContextOps::get_bound_context();
        if (bound == nullptr)
        {
            report_no_current_context("Window::get_context_info");
            return ContextInfo{};
        }

        // 只有该窗口的上下文归本线程 current 时 GL 查询才落在该上下文上
        if (bound != m_native_window)
        {
            report_context_not_current("Window::get_context_info");
            return ContextInfo{};
        }

        // 版本与 profile 只经该上下文上的 GL 入口查询：这些调用按"当前上下文"生效，因此在任何
        // 绑定线程上都成立。平台属性读取接口明写只能在主线程调用，本接口不依赖它
        const GetStringFn get_string = reinterpret_cast<GetStringFn>(glfwGetProcAddress("glGetString"));
        if (get_string == nullptr)
        {
            report_gl_query_failed("Window::get_context_info");
            return ContextInfo{};
        }

        const char* const version_text = reinterpret_cast<const char*>(get_string(kGlVersion));
        const GLVersionReading reading = parse_gl_version(version_text);
        if (!reading.is_valid)
        {
            report_gl_query_failed("Window::get_context_info");
            return ContextInfo{};
        }

        // 全部取实际值（不是 OpenGLContextDesc 的请求值）；is_debug 是软偏好，
        // 与请求值不同不构成失败，这里不比较、也不据此改返回值
        ContextInfo info;
        info.major = reading.major;
        info.minor = reading.minor;

        // 3.2 之前的上下文没有 core/compatibility 之分，profile 查询也会被驱动判为非法枚举
        info.profile = GLProfile::Compatibility;
        if (info.major > 3 || (info.major == 3 && info.minor >= 2))
        {
            const GetIntegervFn get_integer = reinterpret_cast<GetIntegervFn>(glfwGetProcAddress("glGetIntegerv"));
            if (get_integer != nullptr)
            {
                int profile_mask = 0;
                get_integer(kGlContextProfileMask, &profile_mask);
                if ((profile_mask & kGlCoreProfileBit) != 0)
                {
                    info.profile = GLProfile::Core;
                }
            }
        }

        // debug 标志自 GL 4.3 起才是核心查询（GL_CONTEXT_FLAGS）；更低版本没有不污染驱动错误状态的查法，
        // 此时返回 false，实际值只在创建期的日志里记录
        if (info.major > 4 || (info.major == 4 && info.minor >= 3))
        {
            const GetIntegervFn get_integer = reinterpret_cast<GetIntegervFn>(glfwGetProcAddress("glGetIntegerv"));
            if (get_integer != nullptr)
            {
                int context_flags = 0;
                get_integer(kGlContextFlags, &context_flags);
                info.is_debug = (context_flags & kGlDebugFlagBit) != 0;
            }
        }

        return info;
    }

    // ========== 回调注册：只写本窗口的回调表，不触碰平台回调 ==========

    void Window::set_key_callback(KeyCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_key_callback"))
        {
            return;
        }

        CallbackEntry& pressed = m_callbacks[Event::to_event_type_uint(Event::EventType::KeyPressed)];
        pressed.fn.key = fn;
        pressed.user   = user;

        CallbackEntry& released = m_callbacks[Event::to_event_type_uint(Event::EventType::KeyReleased)];
        released.fn.key = fn;
        released.user   = user;

        CallbackEntry& repeat = m_callbacks[Event::to_event_type_uint(Event::EventType::KeyRepeat)];
        repeat.fn.key = fn;
        repeat.user   = user;
    }

    void Window::set_mouse_button_callback(MouseButtonCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_mouse_button_callback"))
        {
            return;
        }

        CallbackEntry& pressed = m_callbacks[Event::to_event_type_uint(Event::EventType::MouseButtonPressed)];
        pressed.fn.mouse_button = fn;
        pressed.user            = user;

        CallbackEntry& released = m_callbacks[Event::to_event_type_uint(Event::EventType::MouseButtonReleased)];
        released.fn.mouse_button = fn;
        released.user            = user;
    }

    void Window::set_cursor_pos_callback(CursorPosCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_cursor_pos_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::MouseMoved)];
        entry.fn.cursor_pos = fn;
        entry.user          = user;
    }

    void Window::set_char_callback(CharCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_char_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::TextInput)];
        entry.fn.char_input = fn;
        entry.user          = user;
    }

    void Window::set_window_close_callback(WindowCloseCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_close_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowClose)];
        entry.fn.window_close = fn;
        entry.user            = user;
    }

    void Window::set_window_size_callback(WindowSizeCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_size_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowResize)];
        entry.fn.window_size = fn;
        entry.user           = user;
    }

    void Window::set_framebuffer_size_callback(WindowSizeCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_framebuffer_size_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowFramebufferResize)];
        entry.fn.framebuffer_size = fn;
        entry.user                = user;
    }

    void Window::set_window_focus_callback(WindowFocusCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_focus_callback"))
        {
            return;
        }

        CallbackEntry& focused = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowFocus)];
        focused.fn.window_focus = fn;
        focused.user            = user;

        CallbackEntry& lost = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowLostFocus)];
        lost.fn.window_focus = fn;
        lost.user            = user;
    }

    void Window::set_window_iconify_callback(WindowIconifyCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_iconify_callback"))
        {
            return;
        }

        CallbackEntry& minimized = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowMinimized)];
        minimized.fn.iconify = fn;
        minimized.user       = user;

        CallbackEntry& restored = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowRestored)];
        restored.fn.iconify = fn;
        restored.user       = user;
    }

    void Window::set_window_maximize_callback(WindowMaximizeCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_maximize_callback"))
        {
            return;
        }

        CallbackEntry& maximized = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowMaximized)];
        maximized.fn.maximize = fn;
        maximized.user        = user;

        CallbackEntry& unmaximized = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowUnmaximized)];
        unmaximized.fn.maximize = fn;
        unmaximized.user        = user;
    }

    void Window::set_window_refresh_callback(WindowRefreshCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_refresh_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowRefresh)];
        entry.fn.refresh = fn;
        entry.user       = user;
    }

    void Window::set_cursor_enter_callback(CursorEnterCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_cursor_enter_callback"))
        {
            return;
        }

        CallbackEntry& entered = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowCursorEntered)];
        entered.fn.cursor_enter = fn;
        entered.user            = user;

        CallbackEntry& left = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowCursorLeft)];
        left.fn.cursor_enter = fn;
        left.user            = user;
    }

    void Window::set_scroll_callback(ScrollCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_scroll_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::MouseScrolled)];
        entry.fn.scroll = fn;
        entry.user      = user;
    }

    void Window::set_file_drop_callback(FileDropCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_file_drop_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::FileDrop)];
        entry.fn.file_drop = fn;
        entry.user         = user;
    }

    void Window::set_window_pos_callback(WindowPosCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_window_pos_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowMoved)];
        entry.fn.window_pos = fn;
        entry.user          = user;
    }

    void Window::set_content_scale_callback(ContentScaleCallback fn, void* user) noexcept
    {
        if (Detail::reject_if_in_callback("Window::set_content_scale_callback"))
        {
            return;
        }

        CallbackEntry& entry = m_callbacks[Event::to_event_type_uint(Event::EventType::WindowContentScaleChanged)];
        entry.fn.content_scale = fn;
        entry.user             = user;
    }
} // namespace ID::Window
