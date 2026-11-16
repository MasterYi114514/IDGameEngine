# IDWindow

薄封装窗口插件：把 vendor GLFW 3.5.1 静态包在内部，对外只暴露"窗口句柄 + 窗口实体 + 事件层"这一层契约。
公开头不含 GLFW / 平台 / Vulkan / IDLog 头，也不出现任何平台类型名；平台侧在公开面只以 `void*` 与整数出现。

- 插件版本：`ID_WINDOW_VERSION_MAJOR` 0 / `ID_WINDOW_VERSION_MINOR` 1（`version_string()` 返回 `"IDWindow 0.1 (GLFW 3.5.1)"`）。
- 接口版本：`ID_WINDOW_INTERFACE_VERSION` 2（与版本 1 的破坏性差异见第 8 节）。
- 链接形态：默认 STATIC；`-DIDWINDOW_BUILD_SHARED=ON` 切 SHARED（Windows 为 DLL + 导入库），两种形态都由本插件构建清单提供。
- 交付方式：源码（公开头 + `src` + vendor GLFW + `README.md` + `LICENSE`）；许可与分发义务见第 12 节。
- 命名空间：`ID::Window`（含 `ID::Window::Event`、`ID::Window::KeyCodes`、`ID::Window::Keyboard`、`ID::Window::Mouse` 四个子域）。

## 1. 能力概述与定位

- **只做窗口与输入事件的基础设施**：创建/销毁窗口、窗口操作与实时查询、GL 上下文绑定与呈现、原生句柄借用与 Vulkan WSI 扩展名需求、按窗口派发输入与窗口事件。
- **不承载渲染、帧循环与 Vulkan 对象**：本插件不含 GL、Vulkan SDK、平台头；不创建 `VkInstance`/`VkSurfaceKHR`、不建交换链、不做 GPU 同步、不提供帧调度，也不做批量/排队/缓存。
- **单一创建入口**：`WindowPool` 是唯一的创建、销毁与实例获取入口，五个静态函数不增不减；`Window` 对象只由池以 placement new 构造在固定存储上。
- **地址稳定**：固定 64 槽，槽地址一旦确定不搬移，因此存活期间的 `Window&` 地址不变（代价与风险见第 7 节）。
- **事件按产生它的窗口派发**：事件只到达产生它的那个窗口自己的回调表，不经过池、不按"当前窗口"转发（第 6 节）。

## 2. 接口清单

签名与字段名逐字取自公开头（`include/**`）；本文不改写任何语义。

### 2.1 插件入口（`IDWindow.hpp`，`namespace ID::Window`）

```cpp
ID_WINDOW_API const char* version_string() noexcept;
ID_WINDOW_API bool init();
ID_WINDOW_API void shutdown() noexcept;
ID_WINDOW_API bool is_initialized() noexcept;
ID_WINDOW_API const char* last_error() noexcept;

ID_WINDOW_API void poll_events() noexcept;
ID_WINDOW_API void wait_events(double timeout_seconds) noexcept;
ID_WINDOW_API void wake_event_loop() noexcept;
```

| 入口 | 成功语义 | 失败/违约行为 | 线程 |
| --- | --- | --- | --- |
| `version_string` | 编译期拼接的静态串，同一模块加载周期内地址与内容不变；不分配、不格式化、不加锁 | 无 | 任意线程 |
| `init` | 按第 4 节顺序建立子系统后返回 `true`；已初始化时直接返回 `true` | 返回 `false` 并写 `last_error()`（本模块日志器已就绪时同时写日志），按已建立项逆序回滚。**② 分支只写错误文本**：此时本模块日志器未就绪，没有任何日志通道 | 主线程 |
| `shutdown` | 先销毁全部存活窗口，再关事件层、销毁本插件日志器句柄、终止平台；未初始化时是无操作 | 平台终止阶段失败只写 `last_error()`（此时本插件日志器已销毁） | 主线程 |
| `is_initialized` | 原子标志观察结果 | 无 | 任意线程（只观察，不授予生命周期持有权） |
| `last_error` | 本线程最近一次失败文本，非 `nullptr`，从未失败时为 `""` | 无 | 任意线程（按线程各自一份） |
| `poll_events` | 转发一次非阻塞事件泵 | 未初始化、或在事件回调栈内调用：写日志与错误文本，不转发 | 主线程 |
| `wait_events` | 阻塞等待直到有事件或超时 | `timeout_seconds` ≤ 0 / NaN / ∞：写日志与错误文本，不等待 | 主线程 |
| `wake_event_loop` | 投递空事件唤醒事件循环 | 未初始化：写日志与错误文本，不转发；在**调用线程自己**的事件回调栈内：同样只写日志与错误文本，不投递 | **任意线程**（唯一允许跨线程的入口） |

### 2.2 窗口池（`IDWindow/WindowPool.hpp`）

```cpp
class ID_WINDOW_API WindowPool
{
public:
    WindowPool() = delete;

    static WindowHandle create_window(const WindowDesc& desc) noexcept;
    static void         destroy_window(WindowHandle handle) noexcept;
    static void         set_current_window(WindowHandle handle) noexcept;
    static Window&      get_current_window() noexcept;
    static Window&      get_window(WindowHandle handle) noexcept;
};
```

| 入口 | 语义 | 失败/违约行为 |
| --- | --- | --- |
| `create_window` | 取空闲槽 → 在槽上构造 `Window` → 打开平台窗口；描述符原样透传，池不读 `render_api` tag、不解释 `render_desc` | 未初始化、64 槽已满、描述符违约（`title == nullptr`、`size` 为 0 或超出平台整数范围）、平台创建失败：返回无效句柄 + 写日志与错误文本，已占用的槽回收 |
| `destroy_window` | 注销平台回调 → 销毁平台窗口 → 显式析构对象 → 归还槽位 | 句柄无效/越界/指向空闲槽：写日志，不改空闲链，不重复析构 |
| `set_current_window` | 只记录"应用层当前窗口"，**不改变 GL 绑定**，也不参与事件路由 | 句柄无效/越界/空闲槽：写日志，current 保持不变 |
| `get_current_window` | 返回 current 窗口引用的拷贝，有效期 = 该窗口存活期间 | 没有 current 属调用方违约：写日志 + `ID_DEBUG_BREAK()` + `std::abort()`（第 7 节） |
| `get_window` | 返回该句柄对应窗口的引用（地址稳定） | 无效/越界/陈旧句柄属调用方违约：写日志 + `ID_DEBUG_BREAK()` + `std::abort()`（第 7 节） |

### 2.3 窗口实体（`IDWindow/Window.hpp`）

`class ID_WINDOW_API Window` 不可构造、不可析构、不可复制、不可移动（六条 `static_assert` 钉住）；引用只由池交出。

```cpp
using FramebufferSize = WindowSize;
```

**15 个回调类型**（16 个注册入口共用它们：帧缓冲尺寸回调复用 `WindowSizeCallback`；每个入口写入 1–3 个事件槽）：

```cpp
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
```

**窗口操作**：

```cpp
void set_title(const char* utf8_title) noexcept;
void set_size(const WindowSize& size) noexcept;
void show() noexcept;
void hide() noexcept;
void focus() noexcept;
void lose_focus() noexcept;
void request_close() noexcept;
void cancel_close() noexcept;
```

**状态查询**（全部即时取值，无缓存）：

```cpp
bool            should_close() const noexcept;
bool            is_visible() const noexcept;
bool            is_focused() const noexcept;
WindowSize      get_size() const noexcept;
FramebufferSize get_framebuffer_size() const noexcept;
WindowScale     get_content_scale() const noexcept;
const char*     get_title() const noexcept;
WindowStatus    get_status() const noexcept;
bool            get_key_state(KeyCode key) const noexcept;
bool            get_mouse_button_state(KeyCode button) const noexcept;
Mouse::Position get_mouse_position() const noexcept;
WindowHandle    get_handle() const noexcept;
```

**GL 与呈现**：

```cpp
void        swap_buffers() noexcept;
void        make_context_current() noexcept;
void        release_context() noexcept;
void        set_vsync(bool enabled) noexcept;
ProcAddress get_proc_address(const char* name) noexcept;
ContextInfo get_context_info() const noexcept;

VulkanWsiRequirements get_vulkan_wsi_requirements() const noexcept;
NativeWindowInfo      get_native_window_info() const noexcept;
```

**回调注册**（每个窗口一份，`fn == nullptr` 表示注销该组）：

```cpp
void set_key_callback(KeyCallback fn, void* user) noexcept;                        // KeyPressed / KeyReleased / KeyRepeat
void set_mouse_button_callback(MouseButtonCallback fn, void* user) noexcept;        // MouseButtonPressed / MouseButtonReleased
void set_cursor_pos_callback(CursorPosCallback fn, void* user) noexcept;            // MouseMoved
void set_char_callback(CharCallback fn, void* user) noexcept;                       // TextInput
void set_window_close_callback(WindowCloseCallback fn, void* user) noexcept;        // WindowClose
void set_window_size_callback(WindowSizeCallback fn, void* user) noexcept;          // WindowResize（逻辑尺寸）
void set_framebuffer_size_callback(WindowSizeCallback fn, void* user) noexcept;     // WindowFramebufferResize（像素尺寸）
void set_window_focus_callback(WindowFocusCallback fn, void* user) noexcept;        // WindowFocus / WindowLostFocus
void set_window_iconify_callback(WindowIconifyCallback fn, void* user) noexcept;    // WindowMinimized / WindowRestored
void set_window_maximize_callback(WindowMaximizeCallback fn, void* user) noexcept;  // WindowMaximized / WindowUnmaximized
void set_window_refresh_callback(WindowRefreshCallback fn, void* user) noexcept;    // WindowRefresh
void set_cursor_enter_callback(CursorEnterCallback fn, void* user) noexcept;        // WindowCursorEntered / WindowCursorLeft
void set_scroll_callback(ScrollCallback fn, void* user) noexcept;                   // MouseScrolled
void set_file_drop_callback(FileDropCallback fn, void* user) noexcept;              // FileDrop
void set_window_pos_callback(WindowPosCallback fn, void* user) noexcept;            // WindowMoved
void set_content_scale_callback(ContentScaleCallback fn, void* user) noexcept;      // WindowContentScaleChanged
```

### 2.4 事件层（`IDWindow/Event/EventManager.hpp`）

```cpp
class ID_WINDOW_API EventManager
{
public:
    EventManager() = delete;

    static void init() noexcept;
    static void shutdown() noexcept;
    static bool is_initialized() noexcept;
    static void enable() noexcept;
    static void disable() noexcept;
    static bool is_enabled() noexcept;
};
```

- 只有"是否已初始化"与"是否启用"两个布尔状态：没有回调表、没有事件队列、没有缓冲，也不是 `Window` 的友元。
- `init`/`shutdown` 由 `ID::Window::init`/`shutdown` 调用；`enable`/`disable` 是全局闸门，关闭时所有窗口的事件被静默丢弃。
- 四个改状态的入口在事件回调栈内调用属调用方违约（写日志、不改现态）；两个 `bool` 查询在回调栈内允许。

### 2.5 类型与常量

| 类型 | 要点 |
| --- | --- |
| `WindowUINT` / `WindowHandle` | `std::uint16_t` / `Common::Handle<WindowUINT, WindowPool>`；句柄即槽下标，**无代数计数器**；默认构造得到无效句柄 |
| `WindowDesc` | 描述符 POD：`const char* title = "IDWindow"`、`WindowSize size = { 1280, 720 }`、`bool is_resizable = true`、`bool is_visible = true`、`bool is_decorated = true`、`RenderApi render_api = RenderApi::None`、`union RenderDesc { OpenGLContextDesc opengl{}; VulkanContextDesc vulkan; } render_desc{}` |
| `RenderApi` | `enum class RenderApi : std::uint8_t { None = 0, OpenGL = 1, Vulkan = 2 }` |
| `OpenGLContextDesc` | `major = 3`、`minor = 3`、`profile = GLProfile::Core`、`is_forward_compatible = false`、`is_double_buffered = true`、`is_debug = false`、`FramebufferPreferences framebuffer = {}`、`WindowHandle share_with = {}` |
| `FramebufferPreferences` | `red_bits/green_bits/blue_bits/alpha_bits = 8`、`depth_bits = 24`、`stencil_bits = 8`、`samples = 0`、`is_srgb = false`（softPreferences，不承诺匹配） |
| `GLProfile` | `enum class GLProfile : std::uint8_t { Core = 0, Compatibility = 1 }` |
| `ContextInfo` | `major = 0`、`minor = 0`、`profile = GLProfile::Core`、`is_debug = false`（实际查询值，不是请求值） |
| `WindowSize` / `WindowPosition` / `WindowScale` | `{std::uint32_t width, height}` / `{std::int32_t x, y}` / `{float x = 1.0f, y = 1.0f}`，无填充 |
| `WindowStatusEnum` / `WindowStatus` | 位标志枚举（`None = 0`、`Visible = 1u << 0`、`Focused = 1u << 1`、`Iconified = 1u << 2`、`Maximized = 1u << 3`、`Resizable = 1u << 4`、`Decorated = 1u << 5`、`ShouldClose = 1u << 6`）+ 只存一个位集合字的包装类；裸位构造是 `explicit` |
| `KeyCode` / `IDKeyCodeUnderlyingType` | `std::uint16_t` 底层宽度的值类型，无效值为底层最大值；`is_valid()`/`get_key()`/`==`/`!=`，无算术 |
| `KeyCodes` | 键盘具名常量（如 `KeyCodes::A`、`KeyCodes::Escape`、`KeyCodes::LeftControl`）与鼠标键（`MouseButton1..8`、`MouseButtonLeft/Right/Middle/X1/X2`、`Unknown`）；成员判定读 `KeyCodes::is_keyboard_key` / `KeyCodes::is_mouse_button` |
| `Keyboard` | `ID_FORCEINLINE constexpr bool is_ctrl/is_shift/is_alt/is_super(KeyCode key) noexcept`，只判键本身，不维护按下状态 |
| `Mouse::Position` / `Mouse::ScrollOffset` | 两个 `double`；位置相对**窗口内容区左上角**，滚动量是本次增量 |
| `ProcAddress` | `using ProcAddress = void(*)();` |
| `NativeBackend` | `enum class NativeBackend : std::uint8_t { None = 0, Win32 = 1, X11 = 2, Wayland = 3, Cocoa = 4 }` |
| `NativeWindowInfo` | `backend` 在偏移 0 + union（`win32.hwnd`/`hinstance`、`x11.display`/`x11_window`、`wayland.display`/`surface`、`cocoa.ns_window`），全部是**借用**；默认构造给 `NativeBackend::None` 与已激活的 `win32` 分支 |
| `VulkanWsiRequirements` | `NativeBackend backend`、`const char* const* extension_names`、`std::uint32_t extension_count` |
| `VulkanContextDesc` | 空描述符占位：本插件不创建任何 Vulkan 对象 |
| `Event::EventType` | `None = 0`、窗口 `WindowClose = 1`/`WindowResize = 2`/`WindowFocus = 3`/`WindowLostFocus = 4`/`WindowMoved = 5`、键盘 `KeyPressed = 6`/`KeyReleased = 7`/`KeyRepeat = 8`、鼠标 `MouseButtonPressed = 9`/`MouseButtonReleased = 10`/`MouseMoved = 11`/`MouseScrolled = 12`、`WindowFramebufferResize = 13`/`WindowContentScaleChanged = 14`/`WindowMinimized = 15`/`WindowRestored = 16`/`WindowMaximized = 17`/`WindowUnmaximized = 18`/`WindowRefresh = 19`/`WindowCursorEntered = 20`/`WindowCursorLeft = 21`、文本与拖放 `TextInput = 22`/`FileDrop = 23`、哨兵 `Count = 24` |
| `Event::Event<T>` / `EventTraits<T>` | 按事件类型携带负载的值类型；`payload` 由 traits 表给出（9 个 payload：`EmptyPayload`、`KeyCode`、`WindowSize`、`WindowPosition`、`WindowScale`、`Mouse::Position`、`Mouse::ScrollOffset`、`Codepoint`、`FileDropList`） |
| `Event::EventContext` | 一次派发的载体：`type` 在偏移 0，`alignas(8)`；字节级往返由它自己的编译期断言保证 |
| `Event::KeyAction` | `enum class KeyAction : std::uint8_t { Released = 0, Pressed = 1, Repeat = 2 }` |
| `Event::Codepoint` | `std::uint32_t value`；独立值类型，不是 `std::uint32_t` 的别名 |
| `Event::FileDropList` | `const char* const* paths`、`std::uint32_t count`；**借用**，只在本次回调期间有效 |

## 3. 最小可编译示例

下面这段代码是可编译的整体（本次任务单独编译、链接并以退出码 0 运行，见第 10 节）。它演示
`init` → `create_window` → 注册回调 → `poll_events` → `destroy_window` → `shutdown` 的完整闭环。

```cpp
#include "IDWindow.hpp"

#include <cstdio>

#include "IDWindow/Event/EventData.hpp"
#include "IDWindow/KeyCode/KeyCodes.hpp"
#include "IDWindow/Window.hpp"
#include "IDWindow/WindowDesc.hpp"
#include "IDWindow/WindowHandle.hpp"
#include "IDWindow/WindowPool.hpp"

namespace
{
    // 回调签名取自 Window::KeyCallback；只读事件载荷 + 调用本窗口的窗口操作（回调栈内允许）
    void on_key(void* /*user*/, ID::Window::Window& window, const ID::Window::Event::KeyEvent& event) noexcept
    {
        if (event.key == ID::Window::KeyCodes::Escape)
        {
            window.request_close();
        }
    }
} // namespace

int main()
{
    // 1) 初始化：整进程一次，主线程
    if (!ID::Window::init())
    {
        std::printf("init failed: %s\n", ID::Window::last_error());
        return 1;
    }

    // 2) 创建窗口：池是唯一创建入口，失败返回无效句柄
    ID::Window::WindowDesc desc;
    desc.title      = "IDWindow minimal example";
    desc.size       = { 960, 540 };
    desc.render_api = ID::Window::RenderApi::None;      // 不建图形上下文

    const ID::Window::WindowHandle handle = ID::Window::WindowPool::create_window(desc);
    if (!handle.is_valid())
    {
        std::printf("create_window failed: %s\n", ID::Window::last_error());
        ID::Window::shutdown();
        return 1;
    }

    // 3) 注册回调：事件只投递给产生它的这个窗口
    ID::Window::Window& window = ID::Window::WindowPool::get_window(handle);
    window.set_key_callback(&on_key, nullptr);

    // 4) 事件泵：事件在平台回调栈内送达本窗口
    for (int frame = 0; frame < 120 && !window.should_close(); ++frame)
    {
        ID::Window::poll_events();
    }

    // 5) 销毁窗口，再关闭子系统（shutdown 也会销毁全部存活窗口）
    ID::Window::WindowPool::destroy_window(handle);
    ID::Window::shutdown();
    return 0;
}
```

构建与链接：

- 消费者需要两条 include 路径：`IDPlugin/IDWindow/include` 与本插件的公开头所依赖的 `IDPlugin/Common`（公开头里的 `IDHandle.hpp`、`Core/Core.hpp` 来自那里）。**不需要** GLFW、IDLog 或 Vulkan 的 include 路径。
- STATIC 形态：定义 `ID_WINDOW_STATIC`（`ID_WINDOW_API` 展开为空），链接本插件的静态库；CMake 消费者直接 `target_link_libraries(app PRIVATE IDWindow)` 即可，vendor GLFW 与 IDLog 的链接项由目标的私有依赖传递。手工命令行链接时需追加 vendor `libglfw.a`、`IDLog` 的导入库，以及 Windows 上的 `gdi32 user32 shell32`。
- SHARED 形态：消费者不定义任何宏（公开头按 `dllimport` 声明），链接导入库，并把 `IDWindow` 的 DLL 放到可执行文件同目录。
- 运行期还依赖 IDLog 的 DLL：IDLog 是 `SHARED` 插件，也需与可执行文件同目录。

## 4. 初始化与关闭顺序

`init()` 的顺序固定，任一步失败时按**已建立项逆序回滚**，并保持"未初始化"的对外状态：

| 步骤 | 建立项 | 失败时的回滚（逆序） |
| --- | --- | --- |
| ① | `ID::Log::init()`（幂等、无失败返回） | 不归本插件回滚：日志插件可能被其它插件共用，本插件不关闭别人的日志系统 |
| ② | 本插件自己的日志器句柄 | 失败即返回 `false`：只写 `last_error()`（此时没有可用的日志通道），已建立项只有 ① |
| ③ | 平台错误回调（必须在 ④ 之前安装，否则拿不到初始化期错误） | 撤销 ③：清掉错误回调 |
| ④ | 平台进程级初始化 | 撤销 ④ 与 ②：先清错误回调、再销毁本插件日志器句柄；**不调用平台终止函数**（vendor GLFW 文档写明平台初始化失败时它已在内部完成清理） |
| ⑤ | 事件层 `Event::EventManager::init()` | 无失败返回，无回滚分支 |
| ⑥ | 复位窗口池（槽存活位、自由链、current） | 无失败返回，无回滚分支 |

- **幂等**：已初始化时 `init()` 直接返回 `true`，不重复初始化，**也不清空 `last_error()`**。
- `shutdown()` 的顺序固定：**先销毁全部存活窗口**（平台要求终止前所有窗口已销毁）→ 关闭事件层 → 销毁本插件自己的日志器句柄 → 清错误回调 → 平台终止 → 复位对外状态。槽表复位随"销毁全部窗口"一并完成。
- **幂等**：未初始化时 `shutdown()` 是无操作；返回后可以再次 `init()`。
- **日志器句柄归属**：本插件在 `init()` 内调用 `ID::Log::init()` 并 `create_logger("IDWindow")`；`shutdown()` 内**不调用** `ID::Log::shutdown()`（不关闭别人的日志系统），只销毁自己的日志器句柄。**完整的日志契约（谁负责 IDLog 的初始化与关闭、句柄在哪一步销毁）待日志专题冻结**，本版按上述现行口径实现。
- 日志口径：一次窗口创建与一次窗口销毁各只产生一条 `INFO`（分别由 `Window::open_platform` 与 `Window::destroy` 给出，池不再重复记录同一事件）；`shutdown` 的批量扫描只记一条 `TRACE`。诊断类记录只落在失败冷路径。
- 日志器取不到时（`init()` 失败或已 `shutdown()`）：日志被静默丢弃，不影响任何返回值，也不影响 `last_error()`。

## 5. 线程与生命周期前提

- `init`/`shutdown`/`poll_events`/`wait_events`、窗口创建与销毁、窗口操作与查询、回调注册都只允许在**平台主线程**调用；`is_initialized()`、`Event::EventManager::is_initialized()`、`Event::EventManager::is_enabled()` 与 `last_error()` 可在任意线程只读观察。这些查询背后的三个状态字（`ID::Window` 的就绪标志、`Event::EventManager` 的"已初始化"与"已启用"）都是 `std::atomic<bool>`，按 `relaxed` 读写：原子性只用来消除跨线程读写的未定义行为，不建立线程间的同步或顺序关系。
- **"第一次 `init()` 所在的线程必须就是平台主线程"是调用方前置条件**：底层平台库没有可跨平台查询的接口，插件只能记录首次 `init` 的线程，**无法验证**它是否真是平台主线程。违反该前提属未定义行为。
- `wake_event_loop()` 是唯一允许跨线程调用的入口；`shutdown()` 之前必须停掉生产者线程。
- 回调栈违约的判定只看**调用线程自己**的派发栈（每线程一份）：其它线程正在派发不影响本线程的判定，主线程处于回调栈内时后台线程调用 `wake_event_loop()` 不会被误拒；同一线程在自己的回调栈内调用仍被拒绝。
- GL 上下文的交接序列（两个方向都由应用负责线程同步，插件的 `owner` 只做诊断）：
  1. 原持有线程 `release_context()`；
  2. 应用自己的同步（信号量、fence、队列等）；
  3. 新线程 `make_context_current()`。
- 同一上下文被两个线程同时绑定时属调用方违约：插件用 `owner` 原子 + 真实绑定复核做诊断并拒绝（写日志、不改绑定），**`owner` 不是并发正确性保证**。不同线程各持不同上下文时，诊断能力会退化。
- 并发回收**不支持**：销毁窗口前必须停止所有 worker 对该窗口的使用（先停渲染线程、完成 GPU 同步，再 `destroy_window`/`shutdown`）。
- 回调签名全部是 `noexcept`：回调里抛异常即 `std::terminate`（本仓库不使用异常）。
- 显式销毁序列（Vulkan 客户端）：应用停止提交 → 同步渲染线程并等待相关 GPU 工作完成 → renderer 销毁 device 子对象、`destroySurface`、`destroyDevice`、`destroyInstance` → 本插件 `destroy_window` → `shutdown`。`shutdown` 会销毁全部存活窗口，但不代为等待 GPU 工作。

## 6. 事件归属

- 事件**只到达产生它的那个窗口**：由该窗口自己的回调表接收，不经过池、不按"当前窗口"转发。后台窗口（非 current）同样能收到自己的窗口事件与输入事件，无需先 `set_current_window`。
- `Event::EventManager::enable`/`disable` 是唯一全局闸门：关闭时所有窗口的事件被静默丢弃（不缓存、不计数、无第二条派发路径）。
- `WindowPool::set_current_window` **只影响** `get_current_window()` 的返回值：不改变 GL 绑定，也不参与事件路由。
- 一个事件类型只对应一个回调槽组；注册 `fn == nullptr` 即注销该组。`FileDropList::paths` 只在本次回调期间有效，拷贝 `FileDropEvent` 不延长寿命。
- 回调栈内**允许**窗口操作、只读查询与 `set_vsync`；**禁止**的入口共 29 个（16 个 `set_*_callback`、`make_context_current`/`release_context`/`swap_buffers`、`init`/`shutdown`、`poll_events`/`wait_events`/`wake_event_loop`、`WindowPool::destroy_window`、`EventManager` 的 `init`/`shutdown`/`enable`/`disable`），调用一律"写日志 + 不改现态"。这里的判定只看**调用线程自己**的派发栈（第 5 节）：`wake_event_loop()` 是唯一允许跨线程的入口，其它线程正在派发不影响本线程调用它的判定。

## 7. 失效与违约用法

- **`destroy_window` / `shutdown` 之后**：所有 `Window*`/`Window&` 与通过 `get_native_window_info` 借到的原生句柄/指针立即悬垂，插件**无法检测**后续使用（未定义行为）。
- **槽位地址会复用**：销毁后新建窗口可能占用同一地址，旧 `Window*` 会指向新对象；句柄是 `std::uint16_t` 的槽下标，**没有代数计数器**，因此陈旧句柄无法识别（数值相同的旧句柄可能命中新窗口）。
- **`get_window` / `get_current_window` 的无效入参属调用方违约**：写日志后触发 `ID_DEBUG_BREAK()`（MSVC `__debugbreak()`，其它平台 `__builtin_trap()`）并 `std::abort()`。
  - 该宏**没有 `NDEBUG` 之类的构建开关，在生产（非 debug）构建下同样终止进程**；它不是"只在 debug 生效的断言"。
  - 终止发生在插件内部，调用方**无法捕获或恢复**，也不会得到返回值。
  - 插件无法代为校验句柄有效性（无代数），只能由调用方保证"句柄仍有效、窗口仍存活"。
- 其它违约用法（写日志、不改现态）：空标题、尺寸为 0 或超范围、非法键码、非 GL 窗口调用 GL 接口、本线程 current 不匹配、上下文已被另一线程持有、共享源仍 current、回调栈内调用禁用入口、事件泵超时参数非法。
- `share_with` 指向已销毁的槽会走 `get_window` 的违约语义（终止）；`share_with` 只在创建期读取一次。
- 回调内 `request_close()` 只置位关闭标志，**不销毁窗口、不触发派发**；销毁时机由应用决定。

## 8. 迁移说明：接口版本 1 → 2

接口版本 2 是**破坏性变更**（`ID_WINDOW_INTERFACE_VERSION` 由 1 升到 2），消费者必须按版本判断兼容性。逐条对应实施计划书 §10 的迁移表：

| 旧 | 新 | 迁移动作 |
| --- | --- | --- |
| `Data::Status` + `Data::ErrorInfo` + 15 项错误码表 | **删除**：失败只写日志与 `last_error()` 文本，返回值只表达业务结果 | 判成功/失败改为"有效句柄 / `bool` / 空指针 / 零值"；不得再按错误码分支 |
| `bool init()` / `void shutdown()` / `const char* last_error()` | 签名不变（`void shutdown() noexcept`、`const char* last_error() noexcept` 明确为 `noexcept`） | 语义收敛：`init` 失败仍返回 `false` 并写日志与错误文本；`shutdown` 先销毁全部存活窗口再终止平台 |
| `create_window(desc) → WindowHandle` | `create_window(const WindowDesc&) → WindowHandle`（签名不变），内部改为固定 storage + placement new + 自由链 | 失败返回无效句柄；成功后用 `get_window(handle)` 取 `Window&` |
| `create_window(desc, Window** out_window)` / `create_opengl_window(..., Window**, share)` | **删除**：单一创建入口；渲染方式由 `WindowDesc::render_api` 与 `render_desc` 联合体表达，共享由 `OpenGLContextDesc::share_with` 表达 | 不再有出参；句柄经返回值取得 |
| `is_alive(handle)` / `try_get(handle)` / `try_get_current()` / `dispatch_to_current(event)` | **删除**：存活与定位只由五个冻结入口承担；事件不经过池 | 事件改由产生它的窗口自己派发；池不提供探测接口 |
| `Window**`/`WindowHandle*`/`bool*`/`WindowState*`/`MousePosition*` 等 `T* out` 出参 | **删除**：操作返回 `void`，查询返回值 | 调用点从"传地址 + 判返回码"改为"接返回值 + 判自身语义"；`const T&` 输入不算出参 |
| `WindowUINT` 曾计划加宽到 64 位（低 32 槽 + 高 32 代数） | **保持 `std::uint16_t`**，`WindowHandle = Common::Handle<WindowUINT, WindowPool>`，**无代数** | 不持久化句柄数值；陈旧句柄不可检测（第 7 节） |
| `ID::Window::Data` 二级数据命名空间、`Data::Status`/`Data::WindowDesc`/`Data::WindowState` | **删除分层**：类型平铺在 `ID::Window`（`WindowStatus`/`WindowDesc`/`WindowStatusEnum`），结果类型可嵌套（`Window::FramebufferSize`） | 类型名与包含路径整体替换；`WindowState` 聚合结构删除，尺寸/缩放分别走 `get_size`/`get_framebuffer_size`/`get_content_scale` |
| `ID::Geometry` 中立几何层 | **删除**：几何 POD 留在 `ID::Window` | `Geometry::Size/Position/Scale` 改为 `ID::Window` 下的同名类型 |
| `Window` 的 `friend` 是 `Common::SlotPool<WindowUINT, Window>` 与 `SlotAllocator`，`Window.hpp` 包含 `SlotPool.hpp` | `friend class WindowPool` + `friend struct PlatformCallbacks`；不再包含 `SlotPool.hpp` | 私有构造、私有析构与 `destroy()` 的调用权转到 `WindowPool`；外部无法构造/析构 `Window` |
| `Window(Window&&)`/`operator=(Window&&)` 有定义 | 复制与移动全部 `delete` | 双持有与二次销毁隐患消失；传递一律用 `Window*`/`Window&` |
| `swap_buffers()`/`make_context_current()`/`set_vsync()` | 保留为 `Window` 成员（同一对象、同一语义），补 `release_context`/`get_proc_address`/`get_context_info` | 不必再传句柄；`vsync` 仍是运行期操作，不进 `WindowDesc` |
| `ID::Window::OpenGL` / `ID::Window::Native` 自由函数命名空间 | **删除**：全部成为 `Window` 成员 | `OpenGL::swap_buffers(handle)` 改为 `window.swap_buffers()`；`ProcAddress` 落在 `ID::Window` |
| `WindowPool::set_current_window` 顺带 `make_context_current()` | `set_current_window` 只记录应用层当前窗口 | GL 绑定改由显式 `Window::make_context_current` 完成；current 不参与事件路由 |
| `WindowDesc` 私有字段无访问器（`title`/`size`/`is_resizable`/`vsync`/`is_hidden`/`is_fullscreen`） | `struct WindowDesc` 公开字段 + `RenderApi` tag + 联合体 | 聚合初始化或逐字段赋值；`vsync` → `set_vsync`；`is_hidden` → `is_visible = false`；全屏本版不做 |
| `ID::EventType`/`ID::EventCategory`/`ID::EventCategoryEnum`/`ID::Event`/`ID::EventContext`/`ID::EventManager`/`ID::KeyCode`/`ID::KeyCodes`/`ID::Keyboard`/`ID::Mouse` | `ID::Window::Event::*`、`ID::Window` 平铺的 `KeyCode`（连同 `IDKeyCodeUnderlyingType`）、`ID::Window::KeyCodes`、`ID::Window::Keyboard`、`ID::Window::Mouse` | 命名空间整体收进 `ID::Window`；头文件路径由 `IDEvent/`、`IDKeyCode/`、`IDInputDevice/` 迁到 `IDWindow/Event/`、`IDWindow/KeyCode/`、`IDWindow/` |
| `EventManager : Common::SingletonEager`，`std::function` 擦除回调表 + `set_callback<type>` + `clean()` + 16 槽队列 + `push_event`（先回调再入队）+ `poll_event` | `ID::Window::Event::EventManager` 只保留 `init`/`shutdown`/`is_initialized`/`enable`/`disable`/`is_enabled` | 注册点从全局单例改到具体窗口对象（16 个 `set_*_callback`）；不再有 `instance()`/`push_event`/`poll_event`；事件只到达产生它的窗口且只到达一次 |
| 事件只转发给 current 窗口 | 事件按产生它的窗口投递 | 后台窗口不必先成为 current 即可收到自己的事件 |
| `EventManager::init` 等返回 `Data::Status` | 返回 `void`（两个查询返回 `bool`） | 不再按错误码分支；未初始化时 `enable`/`disable` 写日志、不改现态 |
| 日志宏无条件解引用全局 `Logger*` | 判空后取用；错误文本通道不依赖日志 | logger 未就绪时丢日志、不崩、不影响返回值与错误文本 |

## 9. 失败与诊断

- **没有错误码**：接口返回值只表达业务结果；失败诊断只有两条通道——日志与 `last_error()` 文本。操作类接口失败时**不改现态**，查询类接口返回零值/`false`/`nullptr`。
- `last_error()` 由**本线程**的定长缓冲承载，写入只做定长字节拷贝（不格式化、不取日志器），因此日志器未创建、已销毁或 IDLog 未初始化时该通道照常可用；缓冲定长 512 字节，超长文本被截断到 511 字节，调用方不应假定拿到完整原文。成功路径不清空该文本；空串只表示"本线程还没有收到过错误"。
- 日志器取不到时日志被静默丢弃，不影响返回值与错误文本。
- **热路径（事件派发、窗口操作与查询、GL 交接）不写日志**：诊断集中在失败冷路径（`ID_NOINLINE` + `ID_COLD`）。本插件自己的代码在这些路径上不加锁、不分配、不格式化；底层平台库内部是否分配不在本插件的控制范围内。**这些路径的实际成本由 `Benchmark` 测量，本文不给数字，也不给量级估计**。

## 10. 已知限制与验证状态

### 10.1 已知限制

- **`ContextInfo::is_debug` 只在 GL 4.3 及以上的上下文可查**：更低版本恒返回 `false`（没有不污染驱动错误状态的查法）。请求值与实际值不同只记录、不构成失败；消费方不能把 `false` 读成"确实不是 debug 上下文"。
- **GL `owner` 只是诊断**：单槽 + 线程令牌只能识别"同一上下文被两个线程同时绑定"这一种违约；不同线程各持不同上下文时诊断退化（后续绑定会覆盖诊断归属），`owner` 不提供并发正确性保证，也不保护并发回收。
- **`swap_buffers` / `set_vsync` 的驱动失败不可检出**：平台接口没有可读的失败返回，且读取平台错误状态会消费掉其它诊断信息，插件不做该检查；这两个调用"未生效"时不会写日志、不会进入 `last_error()`。
- **共享上下文的"已同步"前提无法验证**：`share_with` 在创建期只诊断共享源"存活、有 GL 上下文、未 current"；"共享源已同步"由应用负责，插件无法观测。
- **Vulkan WSI 只实现 Win32 后端**：其它后端的原生借用与扩展表一律返回 `NativeBackend::None` 的零值并写日志；`VulkanContextDesc` 是空描述符，插件不含 Vulkan 头、不创建任何 Vulkan 对象。
- **`get_context_info` 的成本未测量**：它每次要做 2–3 次入口查询与 1–2 次 GL 查询，属诊断用途，不应放在逐帧路径。
- **`lose_focus()` 是空操作**：平台没有"放弃焦点"的调用，本实现只写一条日志，不代表窗口已失去焦点。
- **窗口上限 64**，句柄无代数（第 7 节）；`WindowStatusEnum` 的位宽是单字节，用掉 7 位，尚余 1 位未使用。
- **回调里抛异常即终止**：16 个回调类型都是 `noexcept`。
- **Linux / macOS / MSVC 未测**：本版只在 Windows（MinGW GCC 16.2.0）上构建与运行过；非 Win32 平台分支、MSVC 编译面与 DLL 形态的**运行期**都没有证据。

### 10.2 验证状态

- **构建**：MinGW GCC 16.2.0 / Debug / STATIC 与 SHARED 两形态由本插件自身的构建清单配置并构建，退出码 0（本次任务实测；证据归档在工程 `out/verify/T22/`）。
- **运行期**：本次任务用一个独立探针（只使用公开头）实测了 `init` 幂等、错误文本保留、`shutdown` 销毁全部存活窗口、重复 `shutdown` 无操作、再次 `init`、`last_error()` 在 IDLog 未初始化时可读、热路径零日志；另用最小示例程序验证了示例本身可编译、可链接、可运行。
- **未验证**：本插件**没有 `Benchmark` 正式报告**——正确性矩阵（多窗隔离、事件矩阵、状态位逐位、零分配/零锁探针、跨线程交接压力、Vulkan surface 最小链路）与性能评级都还没有测量结果，本文不给出任何性能数字或评级结论。测量与评级归 `Benchmark` 工程。

## 11. Vulkan WSI 边界

契约正文见实施计划书 §9 与 §4.5，本节只列消费者必须知道的要点：

1. **扩展名表是需求声明，不是保证**：`get_vulkan_wsi_requirements()` 给出的 `VK_KHR_surface`、`VK_KHR_win32_surface` 是该后端做 surface 所需的声明，renderer 必须自己枚举 loader 实际支持项，缺项显式失败。
2. **原生对象是有期限的借用**：`get_native_window_info()` 借出的 window/display 句柄与指针，有效期取"对应窗口仍存活、未 `shutdown()`、模块未卸载"三者最早发生者；窗口一销毁即失效，**复制描述符不延长寿命**。扩展名静态表的有效期到本模块卸载为止。
3. **本插件不含 Vulkan 头、不出现任何 Vulkan 类型**：`VkInstance`/`VkSurfaceKHR`/交换链都由 renderer 用官方类型创建；插件不代建 surface、不做 typed bridge。
4. **renderer 自检**：呈现队列族支持、surface capabilities、resize（0 尺寸 / `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`）；**窗口回调内不建交换链**。MoltenVK 的 portability 枚举扩展与 flag 归 renderer 决定。
5. **首轮仅 Win32**：X11 / Wayland / Cocoa 一律返回零值结果并写日志。外部 `VkSurfaceKHR`/`VkDevice` 仍存活时调用 `destroy_window` 属契约违约，插件**无法检测**（不持有外部 Vulkan 对象），也不代为等待。

## 12. 许可与分发

许可事实集中在插件根的 `LICENSE`，本节与它互为引用；两者与 vendor 原文冲突时以 vendor 原文为准。

- **本插件源码的授权条款待仓库决策**：仓库根当前没有 LICENSE / COPYING，插件源码的授权不由本插件自行拟定；`LICENSE` 第一节记录该现状，仓库层级确定授权后在同一处补充，本节不代填。
- **vendor GLFW 3.5.1 采用 zlib/libpng**：许可原文在 `IDPlugin/Vendor/glfw-3.5.1/LICENSE.md`（不得删改）；`LICENSE` 第三节照录同一份原文，只用于交付包单独查阅。
- **四条分发义务**（与 `LICENSE` 第二节逐条对应）：

  1. vendor 目录内的 `LICENSE.md` 必须原样随交付资产留存，不得删除、裁剪或改写；本插件不复制第二份可能被误当成原文的副本。
  2. 分发本插件（源码或二进制、发行包或安装包）时必须同时分发该 `LICENSE.md`。
  3. 若交付前对 vendor 源码做过改动，按 zlib 许可第 2 条必须在分发时明确标注为修改版本；本插件不声称 vendor 已被或未被改动，实际内容以 vendor 目录为准。
  4. `LICENSE` 第三节的抄录只用于交付包单独查阅；与 `IDPlugin/Vendor/glfw-3.5.1/LICENSE.md` 冲突时以该原文为准。

- **消费者需要知道的边界**：vendor GLFW 以 `PRIVATE` 方式静态链接（SHARED 形态下同样打进 `IDWindow` 的动态库），消费者不需要 GLFW 的 include 路径与链接项；但分发含本插件动态库的产品仍属第 2 条意义上的分发，必须带 `LICENSE.md`。
