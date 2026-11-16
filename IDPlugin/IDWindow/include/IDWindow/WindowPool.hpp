#pragma once

#include "IDWindowCore.hpp"

#include "IDWindow/Window.hpp"
#include "IDWindow/WindowDesc.hpp"
#include "IDWindow/WindowHandle.hpp"

/**
 *  窗口池：唯一的创建、销毁与实例获取入口，不可实例化（全部 `static`）。
 *
 *  形态与边界：
 *  1. 固定 64 槽，实现在 `src/Window/WindowPool.cpp` 内以固定 `std::byte` 存储 + placement new +
 *     显式析构 + 自由链完成：不扩容、不搬移，也不使用会增长并搬移元素的下标池。
 *     槽地址一旦确定就不再变化，因此"存活期间的 `Window&` 地址稳定"成立。
 *  2. 只管生命周期与实例获取：不读 `RenderApi` tag、不解释描述符的 `union`、不含平台头、
 *     不接触 GLFW/GL/Vulkan 类型。渲染与 WSI 查询都是 `Window` 的成员。
 *  3. 句柄即槽下标（`WindowUINT` 是 `std::uint16_t`），没有代数计数器：陈旧句柄与悬垂引用都
 *     不可检测，销毁后不得继续使用旧句柄或旧引用。
 *  4. 五个入口都在平台主线程调用；失败只写日志与错误文本。
 */
namespace ID::Window
{
    class ID_WINDOW_API WindowPool
    {
    public:
        WindowPool() = delete;                  // 只提供静态接口，不允许实例化

        /**
         *  @brief 创建一个窗口：取空闲槽 → 在槽上构造 `Window` → 打开平台窗口
         *  @param desc 创建描述；池原样透传给 `Window`，不读 tag、不解释分支
         *  @return 成功时返回有效句柄；未初始化、64 槽已满、描述符违约（尺寸为 0 或超出平台整数
         *          表达范围、标题为 `nullptr`）或平台创建失败时返回无效句柄，已占用的槽被回收
         *  @note 句柄即槽下标，不做代数校验；创建失败时写日志与错误文本
         */
        static WindowHandle create_window(const WindowDesc& desc) noexcept;

        /**
         *  @brief 销毁一个窗口：注销平台回调、销毁平台窗口、显式析构对象、归还槽位
         *  @param handle 由 `create_window` 返回的句柄
         *  @note 句柄无效、越界或指向空闲槽时写日志且不改空闲链，不重复析构
         *  @note 关闭请求标志不会自动触发销毁；销毁后该窗口的 `Window&` 与原生借用立即失效
         */
        static void         destroy_window(WindowHandle handle) noexcept;

        /**
         *  @brief 记录应用层当前窗口
         *  @param handle 已在使用的有效句柄
         *  @note 只决定 `get_current_window()` 的返回值：**不改变 GL 上下文绑定**，也不参与事件路由；
         *        句柄无效、越界或指向空闲槽时写日志、current 保持不变
         */
        static void         set_current_window(WindowHandle handle) noexcept;

        /**
         *  @brief 取应用层当前窗口
         *  @return current 窗口的引用，有效期 = 该窗口存活期间
         *  @note 没有 current（从未设置或已销毁）属调用方违约：写日志 + `ID_DEBUG_BREAK()`，
         *        该宏在生产构建下同样终止进程，没有错误通道可返回
         */
        static Window&      get_current_window() noexcept;

        /**
         *  @brief 按句柄取窗口
         *  @param handle 由 `create_window` 返回且仍存活的句柄
         *  @return 该窗口的引用（地址稳定），有效期 = 该窗口存活期间
         *  @note 无效、越界或陈旧句柄属调用方违约：写日志 + `ID_DEBUG_BREAK()`，
         *        该宏在生产构建下同样终止进程，没有错误通道可返回
         */
        static Window&      get_window(WindowHandle handle) noexcept;
    };
} // namespace ID::Window
