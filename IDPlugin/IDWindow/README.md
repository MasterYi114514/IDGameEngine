# ID.Window

窗口能力插件。对下游交付一个**运行期函数表**（服务槽 `9`）与一组编译期常量，用于创建/销毁唯一的
平台窗口、逐帧处理事件、查询帧缓冲尺寸与取平台原生窗口句柄。

- 插件名 / target / 命名空间：`ID.Window` / `IDWindow` / `ID::Window`
- 形态：`kind=module`、`lifecycle=standard`、`provides.slot=9`、`provides.api_revision=1`
- 上游依赖：`ID.Log`（`api_revision` 必须落在 `[2, 2]`），用于报出 GLFW 的错误与就绪信息

## 1. 公开接口

只有两个头参与对下游的契约：

| 头 | 内容 |
|---|---|
| `IDWindowApi.hpp` | `kWindowApiRevision`、`kWindowSlot`、`WindowConfig`、`WindowApiTable` |
| `IDWindowLifecycle.hpp` | 四个生命周期回调声明（由组装绑定 TU 取址，消费者不用、也不该调用） |

`IDWindowApi.hpp` **不包含任何 GLFW 头**：GLFW 以插件私有方式从 `vendor/glfw` 静态链入本插件，
它的类型、宏与 include 路径都不进入下游的编译单元。窗口句柄也不跨 DLL 暴露。

`WindowApiTable` 的入口：

| 入口 | 语义 | 失败返回 |
|---|---|---|
| `bool open(const WindowConfig&)` | 创建唯一窗口（`glfwInit` 已由 `on_init` 完成） | 已有窗口 / 不在 Ready 区间 / `glfwCreateWindow` 失败 → `false`，状态不变 |
| `void close()` | 销毁窗口（幂等） | — |
| `bool poll_events()` | 处理一批待处理事件 | 无窗口、或已请求关闭 → `false` |
| `bool should_close()` | 查询关闭请求 | 无窗口 → `true` |
| `void framebuffer_size(int32_t*, int32_t*)` | 帧缓冲尺寸（HiDPI 下与逻辑尺寸不同） | 无窗口时两个输出参数都不写 |
| `void* native_handle()` | Win32 上为 `HWND`；其它平台为 `nullptr` | 无窗口 → `nullptr` |

## 2. 使用示例

```cpp
#include <cstdint>

#include "IDWindowApi.hpp"      // 只能经真实上游 target 的 PUBLIC include 拿到
#include "Kernel/PluginRuntime.hpp"

#if !defined(ID_USE_PLUGIN_IDWINDOW)
    #error "consumer must receive ID_USE_PLUGIN_IDWINDOW from the IDWindow PUBLIC usage requirements"
#endif
static_assert(ID::Window::kWindowApiRevision == 1, "consumer is adapted to ID.Window revision 1");

// Ready 区间内取表：每次调用只做固定槽位索引 + 一次指针 load，不查字符串、不加锁
const ID::Window::WindowApiTable* const window =
    ID::PluginRuntime::instance().get_service<ID::Window::WindowApiTable>(
        static_cast<ID::ServiceSlot>(ID::Window::kWindowSlot));
if (window == nullptr || window->open == nullptr)
{
    return false;   // 表不存在或表项为空：按"本产品没有窗口能力"显式处理，不静默继续
}

ID::Window::WindowConfig config{};
config.width  = 1600;
config.height = 900;
// config.title 必须以 NUL 结尾；首字节为 NUL 时用插件默认标题
config.title[0] = 'I'; config.title[1] = 'D'; config.title[2] = '\0';
if (!window->open(config))
{
    return false;
}

// 逐帧：表指针在循环外取一次，循环内只做一次间接调用
while (window->poll_events())
{
    int32_t width  = 0;
    int32_t height = 0;
    window->framebuffer_size(&width, &height);
    if (width == 0 || height == 0)
    {
        continue;   // 0 表示最小化：渲染侧必须跳过绘制
    }
    // ... 绘制 ...
}
window->close();
```

## 3. 线程与时序前提

- 全部入口只允许在**同一控制线程**（GLFW 初始化、创建窗口、轮询事件的线程）上调用；
  GLFW 的窗口与事件接口不是线程安全的，插件不加锁兜底。
- `WindowApiTable` 指针只在 Ready 区间内有效：本插件 `on_init` 成功返回之后，到 `on_shutdown`
  之前；`on_shutdown` 之后表地址失效，必须重新经 `get_service` 取得。
- `open` 必须在 `on_init` 之后调用：`on_init` 只负责 `glfwInit`，不替产品决定窗口尺寸、可见性
  与图形上下文。
- 单窗口策略：`open` 重复调用返回 `false`；需要重建窗口时先 `close`。

## 4. 代价与性能口径

- 每次表项调用是**一次间接调用**，跨 DLL 不能被内联；表指针取用是固定槽位索引 + 一次指针 load。
- 所有入口都是低频路径（初始化、场景构建、逐帧一次），但 `poll_events` 一次调用就会处理整批事件；
  **不要**为每个事件各调一次，也不要在逐元素路径上引入本表的调用。
- 本表不提供逐元素接口，也不承担输入事件分发：需要按键/鼠标回调时由后续插件在 GLFW 之上另建能力，
  而不是把逐事件回调塞进本表。

## 5. 已知边界（不做的事）

- 只实现 Win32 后端；非 Windows 平台 `native_handle` 返回 `nullptr`，不提供伪句柄。
- 不做多窗口、窗口池、窗口句柄表；不做隐藏/最小化/置顶等运行期窗口属性修改（本修订只有创建参数）。
- 不做渲染、不交换缓冲、不创建/绑定图形 API 的 surface；`WindowConfig` 只决定请求哪种图形上下文。
- 不做运行期 GLFW 重新初始化或后端切换；`glfwTerminate` 只在 `on_shutdown` 调用一次。

## 6. 第三方与许可证

`vendor/glfw/` 是 GLFW 源码（版本见其 `CMakeLists.txt` 的 `project(GLFW VERSION …)`），
以静态库形式随本插件构建，开关与原因写在 `CMakeLists.txt` 的注释里（关闭文档/示例/测试/安装，
MSVC 侧切到静态 CRT 以与插件统一）。

GLFW 的许可证是其自带的 `vendor/glfw/LICENSE.md`（zlib/libpng 风格），分发前按该文件执行；
本插件不修改 GLFW 源码。
