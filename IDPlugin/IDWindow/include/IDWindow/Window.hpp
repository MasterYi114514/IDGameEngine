#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include "IDWindowCore.hpp"

#include "IDWindow/ContextInfo.hpp"
#include "IDWindow/Event/EventContext.hpp"
#include "IDWindow/Event/EventData.hpp"
#include "IDWindow/NativeWindowInfo.hpp"
#include "IDWindow/ProcAddress.hpp"
#include "IDWindow/VulkanWsiRequirements.hpp"
#include "IDWindow/WindowDesc.hpp"
#include "IDWindow/WindowHandle.hpp"
#include "IDWindow/WindowStatus.hpp"

/**
 *  窗口实体：由 `WindowPool` 以 placement new 构造在固定存储上，地址在整个存活期间不变。
 *
 *  形态与所有权：
 *  1. 构造、析构、`destroy()`、`open_platform()`、事件入口全部私有，只有一个 friend 管生命周期
 *     （`WindowPool`）与一个 friend 做平台回调桥（`PlatformCallbacks`，本头只前置声明、定义只在
 *     `src/Window/Window.cpp`）。因此外部既不能构造/析构，也不能 `new`/`delete`：`Window` 不可析构。
 *  2. 复制与移动全部 `delete`：地址稳定、句柄唯一，不存在两个对象持有同一平台窗口的可能。
 *  3. 平台窗口指针在公开头里只以 `void*` 出现，真实类型只在实现文件内还原；本头不含任何平台头、
 *     GLFW 头、Vulkan 头与 IDLog 头，也不出现平台类型名。
 *  4. 状态一律即时查询：本类不缓存尺寸、缩放、可见性、输入状态，每次调用都向平台取当场值。
 *  5. `Window&` 的有效期 = 该窗口存活期间；`destroy_window`/`shutdown` 之后原引用悬垂，插件无法检测。
 *  6. 除 GL 绑定与呈现外，所有成员都只允许在平台主线程调用；`make_context_current`/`release_context`/
 *     `swap_buffers`/`set_vsync`/`get_proc_address`/`get_context_info` 要求调用线程是当前的绑定线程
 *     （交接方向不受此限，见各成员注释）。失败只写日志与错误文本，操作类接口不改现态、查询类接口
 *     返回零值/`false`/`nullptr`，没有错误码返回值。
 *  7. 事件在产生它的窗口自己的回调栈内派发（开关闸门、回调表与派发深度都在 `Window::on_event`）。
 *     回调栈内**允许**窗口操作、只读查询与 `set_vsync`；回调栈内**禁止**的入口共 29 个，调用一律
 *     "写日志 + 不改现态"：
 *       ① 16 个 `set_*_callback`；
 *       ② 3 个 GL 绑定与呈现入口 `make_context_current`/`release_context`/`swap_buffers`；
 *       ③ 生命周期 `init`/`shutdown`；
 *       ④ 事件泵 `poll_events`/`wait_events`/`wake_event_loop`；
 *       ⑤ `WindowPool::destroy_window`；
 *       ⑥ 事件层 `EventManager::init`/`shutdown`/`enable`/`disable`。
 *
 *  命名特例（理由见实施计划书 §3.3）：`FramebufferSize` 是嵌套别名，不重复插件前缀；
 *  参数与返回值里的 `WindowSize`/`WindowPosition`/`WindowScale`/`KeyCode`/`Mouse::*` 都在 `ID::Window`
 *  平铺层或 `ID::Window::Mouse` 下，不写二级限定。
 */
namespace ID::Window
{
    class WindowPool;
    struct PlatformCallbacks;    // 平台回调桥：本头只前置声明，定义只在 src/Window/Window.cpp

    class ID_WINDOW_API Window
    {
    public:
        using FramebufferSize = WindowSize;     // 与逻辑尺寸同类型，由接口区分语义

        using KeyCallback            = void(*)(void* user, Window& window, const Event::KeyEvent& event) noexcept;
        using MouseButtonCallback    = void(*)(void* user, Window& window, const Event::MouseButtonEvent& event) noexcept;
        using CursorPosCallback      = void(*)(void* user, Window& window, const Event::CursorPosEvent& event) noexcept;
        using CharCallback           = void(*)(void* user, Window& window, const Event::CharEvent& event) noexcept;
        using WindowCloseCallback    = void(*)(void* user, Window& window, const Event::WindowCloseEvent& event) noexcept;
        using WindowSizeCallback     = void(*)(void* user, Window& window, const Event::WindowSizeEvent& event) noexcept;
        using WindowFocusCallback    = void(*)(void* user, Window& window, const Event::WindowFocusEvent& event) noexcept;
        using WindowIconifyCallback  = void(*)(void* user, Window& window, const Event::WindowIconifyEvent& event) noexcept;
        using WindowMaximizeCallback = void(*)(void* user, Window& window, const Event::WindowMaximizeEvent& event) noexcept;
        using WindowRefreshCallback  = void(*)(void* user, Window& window, const Event::WindowRefreshEvent& event) noexcept;
        using CursorEnterCallback    = void(*)(void* user, Window& window, const Event::CursorEnterEvent& event) noexcept;
        using ScrollCallback         = void(*)(void* user, Window& window, const Event::ScrollEvent& event) noexcept;
        using FileDropCallback       = void(*)(void* user, Window& window, const Event::FileDropEvent& event) noexcept;
        using WindowPosCallback      = void(*)(void* user, Window& window, const Event::WindowPosEvent& event) noexcept;
        using ContentScaleCallback   = void(*)(void* user, Window& window, const Event::ContentScaleEvent& event) noexcept;

        // ========== 窗口操作 ==========

        /**
         *  @brief 设置窗口标题
         *  @param utf8_title 非空 UTF-8 NUL 终止串；`nullptr` 或空串属调用方违约（写日志、不改现态）
         */
        void set_title(const char* utf8_title) noexcept;

        /**
         *  @brief 设置窗口逻辑尺寸
         *  @param size 宽高都必须大于 0 且可被平台整数表达；违约时写日志、不改现态
         *  @note 只设逻辑尺寸，帧缓冲尺寸由系统行为决定，随时用 `get_framebuffer_size()` 查
         */
        void set_size(const WindowSize& size) noexcept;

        void show() noexcept;
        void hide() noexcept;
        void focus() noexcept;

        /**
         *  @brief 释放本插件对该窗口的焦点请求
         *  @note 平台没有"放弃焦点"的调用：本实现按空操作处理并写一条日志，不代表窗口已失去焦点；
         *        真实焦点状态以 `WindowFocus`/`WindowLostFocus` 事件与 `get_status()` 为准
         */
        void lose_focus() noexcept;

        /**
         *  @brief 置位窗口的关闭请求标志
         *  @note 不自动销毁窗口、不触发任何派发；应用仍需自行决定何时 `destroy_window`
         */
        void request_close() noexcept;

        /**
         *  @brief 复位窗口的关闭请求标志
         */
        void cancel_close() noexcept;

        // ========== 状态查询（全部即时取值，无缓存） ==========

        bool        should_close() const noexcept;
        bool        is_visible() const noexcept;
        bool        is_focused() const noexcept;

        WindowSize      get_size() const noexcept;
        FramebufferSize get_framebuffer_size() const noexcept;
        WindowScale     get_content_scale() const noexcept;

        /**
         *  @brief 取窗口标题（借用指针）
         *  @return 平台侧当前标题，UTF-8；窗口销毁或下一次 `set_title` 之后失效
         */
        const char* get_title() const noexcept;

        /**
         *  @brief 取窗口状态位集合
         *  @return 现场组合可见性、焦点、最小化、最大化、可缩放、装饰与关闭请求标志后的状态；
         *          不含成功/失败语义
         */
        WindowStatus get_status() const noexcept;

        /**
         *  @brief 查某个键当前是否处于按下状态
         *  @param key 必须是 `ID::Window::KeyCodes::is_keyboard_key` 认可的已声明键码；
         *             域内跳号与未知键值属违约（写日志并返回 false）
         */
        bool get_key_state(KeyCode key) const noexcept;

        /**
         *  @brief 查某个鼠标键当前是否处于按下状态
         *  @param button 必须落在 `ID::Window::KeyCodes` 的鼠标键域内（违约时写日志并返回 false）
         */
        bool get_mouse_button_state(KeyCode button) const noexcept;

        /**
         *  @brief 取光标位置
         *  @return 相对**窗口内容区左上角**的坐标；不是桌面绝对坐标，也不是帧缓冲像素
         */
        Mouse::Position get_mouse_position() const noexcept;

        /**
         *  @brief 取本窗口自己的句柄
         *  @return 句柄值的拷贝，可用于共享上下文等需要句柄的场合
         */
        WindowHandle get_handle() const noexcept;

        // ========== GL 与呈现 ==========

        /**
         *  @brief 呈现该窗口的后备缓冲
         *  @note 前置：本线程 current 的上下文必须是**该窗口自己的**上下文（由 `make_context_current`
         *        建立）。不匹配时写日志、不调用平台，不会把别的窗口的后备缓冲呈现出去
         *  @note 未初始化、窗口已销毁、不是 GL 上下文窗口、回调栈内调用同样写日志且不调用平台
         */
        void swap_buffers() noexcept;

        /**
         *  @brief 把该窗口的 GL 上下文绑定到调用线程
         *  @note 允许由**尚未持有该上下文的新线程**调用：交接方向是"原持有线程 `release_context()` →
         *        应用自己的同步 → 新线程 `make_context_current()`"
         *  @note 实现先用归属诊断把 owner 转给本线程，再以驱动的真实绑定复核为准；驱动没有生效时
         *        回滚 owner 并写日志。同线程同上下文直接短路，热路径不加锁
         *  @note 该上下文已被**另一线程**持有时属调用方违约：写日志、不调用驱动、不改绑定与 owner。
         *        owner 只是诊断，交接的线程同步仍由应用负责
         */
        void make_context_current() noexcept;

        /**
         *  @brief 解除调用线程的 current 绑定（幂等）
         *  @note 只清本线程的绑定，不动其它线程；本线程本来没有 current 也算成功
         *  @note 窗口已销毁、未初始化、回调栈内调用时写日志且不调用平台
         */
        void release_context() noexcept;

        /**
         *  @brief 设置交换间隔（垂直同步）
         *  @param enabled true 表示等待垂直同步
         *  @note 只作用于调用线程**当前**的上下文，且该上下文必须属于本窗口：本线程没有 current、或
         *        current 是别的窗口时写日志、不调用平台
         *  @note 对 `Vulkan`/`None` 窗口无意义（写日志、不调用平台）；回调栈内允许，前提同上
         */
        void set_vsync(bool enabled) noexcept;

        /**
         *  @brief 取图形 API 入口函数地址
         *  @param name 非空的入口名
         *  @return 该入口在本线程 current 上下文上的地址；`nullptr` 表示名字为空、本线程没有 current、
         *          current 不是该窗口的上下文，或该上下文查不到该入口
         *  @note 必须在**正确的 current 上下文**上查询：不假设不同上下文返回同一地址；
         *        查不到某入口是扩展探测的常规结果，不算失败、也不写日志
         */
        ProcAddress get_proc_address(const char* name) noexcept;

        /**
         *  @brief 查询该窗口 GL 上下文的实际取值
         *  @return 实际值（版本、profile、`is_debug`）：版本与 profile 取自该上下文上的 GL 查询，
         *          不是 `OpenGLContextDesc` 的请求值
         *  @note 只在**该窗口的上下文归本线程 current** 时可用：没有 current、或 current 是别的窗口时
         *        写日志并返回零值
         *  @note `is_debug` 只在 GL 4.3 及以上的上下文可查；更低版本返回 `false`（没有不污染驱动错误
         *        状态的查法），请求值与实际值不同只记录、不构成失败
         */
        ContextInfo get_context_info() const noexcept;

        /**
         *  @brief 取该后端做 Vulkan surface 所需的实例扩展名表
         *  @return `render_api` 为 `RenderApi::Vulkan` 的窗口在 Win32 后端返回
         *          `backend == NativeBackend::Win32`、两条扩展名（`VK_KHR_surface`、`VK_KHR_win32_surface`）
         *          与 `extension_count == 2`；该窗口不是 Vulkan 窗口、或后端未实现时写日志并返回
         *          `extension_names == nullptr`、`extension_count == 0`、`backend == NativeBackend::None`
         *  @note 扩展名表是**需求声明**，不是保证：renderer 必须自己枚举 loader 实际支持项，缺项显式失败
         *  @note 表是模块加载期的静态数据，有效期到本模块卸载为止；不需要 GL 上下文为 current。
         *        本版只实现 Win32 后端（见 `VulkanWsiRequirements.hpp`）
         */
        VulkanWsiRequirements get_vulkan_wsi_requirements() const noexcept;

        /**
         *  @brief 取原生窗口借用信息
         *  @return Win32 后端返回 `backend == NativeBackend::Win32` 与借用的窗口句柄、实例句柄；
         *          后端未实现时写日志并返回 `backend == NativeBackend::None` 的全零结果
         *  @note 借用不转移所有权：有效期 = 窗口存活 且 未 `shutdown` 且 模块未卸载，以三者最早者为止；
         *        窗口销毁即失效，复制本描述符不延长寿命
         *  @note 本版只实现 Win32 后端（见 `NativeWindowInfo.hpp`）
         */
        NativeWindowInfo get_native_window_info() const noexcept;

        // ========== 回调注册（每个窗口一份，按事件类型分槽） ==========

        /**
         *  @brief 注册/注销键盘回调：一次写入 `KeyPressed`、`KeyReleased`、`KeyRepeat` 三个槽
         *  @param fn 回调函数指针；`nullptr` 表示注销该组
         *  @param user 不做解释的用户指针，原样回传给回调
         *  @note 只写本窗口的回调表，不触碰平台回调；回调内注册属调用方违约
         */
        void set_key_callback(KeyCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销鼠标键回调：一次写入 `MouseButtonPressed`、`MouseButtonReleased` 两个槽
         *  @param fn 回调函数指针；`nullptr` 表示注销该组
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_mouse_button_callback(MouseButtonCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销光标移动回调（`MouseMoved`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_cursor_pos_callback(CursorPosCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销字符输入回调（`TextInput`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_char_callback(CharCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销窗口关闭回调（`WindowClose`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_close_callback(WindowCloseCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销逻辑尺寸变化回调（`WindowResize`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_size_callback(WindowSizeCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销帧缓冲尺寸变化回调（`WindowFramebufferResize`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_framebuffer_size_callback(WindowSizeCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销焦点回调：一次写入 `WindowFocus`、`WindowLostFocus` 两个槽
         *  @param fn 回调函数指针；`nullptr` 表示注销该组
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_focus_callback(WindowFocusCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销最小化回调：一次写入 `WindowMinimized`、`WindowRestored` 两个槽
         *  @param fn 回调函数指针；`nullptr` 表示注销该组
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_iconify_callback(WindowIconifyCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销最大化回调：一次写入 `WindowMaximized`、`WindowUnmaximized` 两个槽
         *  @param fn 回调函数指针；`nullptr` 表示注销该组
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_maximize_callback(WindowMaximizeCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销窗口内容重绘回调（`WindowRefresh`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_refresh_callback(WindowRefreshCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销光标进入/离开回调：一次写入 `WindowCursorEntered`、`WindowCursorLeft` 两个槽
         *  @param fn 回调函数指针；`nullptr` 表示注销该组
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_cursor_enter_callback(CursorEnterCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销滚轮回调（`MouseScrolled`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_scroll_callback(ScrollCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销文件拖放回调（`FileDrop`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         *  @note 回调里的路径数组只在本次调用期间有效
         */
        void set_file_drop_callback(FileDropCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销窗口位置变化回调（`WindowMoved`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_window_pos_callback(WindowPosCallback fn, void* user) noexcept;

        /**
         *  @brief 注册/注销内容缩放变化回调（`WindowContentScaleChanged`）
         *  @param fn 回调函数指针；`nullptr` 表示注销
         *  @param user 不做解释的用户指针，原样回传给回调
         */
        void set_content_scale_callback(ContentScaleCallback fn, void* user) noexcept;

    private:
        friend class WindowPool;            // 生命周期：私有构造、私有析构与 destroy() 只由池调用
        friend struct PlatformCallbacks;    // 平台回调桥：静态回调取回 Window* 后转交 on_platform_event

        Window() = delete;                                              // 不允许默认构造
        explicit Window(const WindowDesc& desc, WindowHandle handle) noexcept;   // 只保存描述符与句柄，不碰平台
        ~Window();                                                      // 私有但必须有定义：池用显式析构调用

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        void destroy() noexcept;                                        // 只由池调用
        bool open_platform() noexcept;                                  // 唯一读 RenderApi tag 与 union 的地方
        void on_platform_event(const Event::EventContext& event) noexcept;
        void on_event(const Event::EventContext& event) noexcept;

        // 每个成员都是同宽函数指针，宽度随目标架构变化
        union CallbackFn
        {
            KeyCallback             key;
            MouseButtonCallback     mouse_button;
            CursorPosCallback       cursor_pos;
            CharCallback            char_input;
            WindowCloseCallback     window_close;
            WindowSizeCallback      window_size;
            WindowSizeCallback      framebuffer_size;
            WindowFocusCallback     window_focus;
            WindowIconifyCallback   iconify;
            WindowMaximizeCallback  maximize;
            WindowRefreshCallback   refresh;
            CursorEnterCallback     cursor_enter;
            ScrollCallback          scroll;
            FileDropCallback        file_drop;
            WindowPosCallback       window_pos;
            ContentScaleCallback    content_scale;
        };

        static_assert(sizeof(CallbackFn) == sizeof(void*), "回调联合体必须与指针同宽，否则未注册判定与槽位搬运不成立");

        // 条目宽度 = 一个函数指针 + 一个 void*：对齐到条目宽度，条目才不会跨 cache 线
        struct alignas(2 * sizeof(void*)) CallbackEntry
        {
            CallbackFn  fn   = {};          // 只写当前事件类型对应的成员；该成员为 nullptr 表示未注册
            void*       user = nullptr;
        };

        static_assert(sizeof(CallbackEntry) == 2 * sizeof(void*), "回调条目必须是函数指针 + void*，不留额外填充");
        static_assert(alignof(CallbackEntry) == 2 * sizeof(void*), "回调条目必须按条目宽度对齐，否则条目会跨 cache 线");

        void*               m_native_window  = nullptr;     // 平台窗口指针：真实类型只在 .cpp 内还原
        WindowHandle        m_handle         = {};          // 本窗口句柄，由池在构造时给出
        WindowDesc          m_desc{};                       // 创建参数副本；title 是借用指针，只在 open_platform 内使用
        bool                m_has_gl_context = false;       // 是否有 GL 上下文：由 open_platform 在创建期决定、运行期不变
        std::array<CallbackEntry, Event::to_event_type_uint(Event::EventType::Count)> m_callbacks{};
    };

    static_assert(!std::is_destructible_v<Window>, "Window 必须不可析构：只能由 WindowPool 显式析构");
    static_assert(!std::is_default_constructible_v<Window>, "Window 不允许默认构造，只能由 WindowPool 创建");
    static_assert(!std::is_copy_constructible_v<Window>, "Window 必须不可复制：地址稳定与句柄唯一依赖这一点");
    static_assert(!std::is_copy_assignable_v<Window>, "Window 必须不可复制赋值：地址稳定与句柄唯一依赖这一点");
    static_assert(!std::is_move_constructible_v<Window>, "Window 必须不可移动：移动会破坏地址稳定");
    static_assert(!std::is_move_assignable_v<Window>, "Window 必须不可移动赋值：移动会破坏地址稳定");
} // namespace ID::Window
