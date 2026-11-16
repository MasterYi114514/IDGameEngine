#pragma once

#include "Core/Core.hpp"
#include "IDLog.hpp"

/**
 *  IDWindow 的生命周期钩子（模块私有头，只被本插件的 `.cpp` 与 `src/Log/Log.cpp` 包含）。
 *
 *  每个钩子都由 `ID::Window::init()` / `ID::Window::shutdown()` 按固定顺序调用，不依赖静态初始化；
 *  声明与实现的对应关系写在每个钩子上方，避免出现"只声明不定义"的接口。
 */
namespace ID::Window::LifeCycle
{
    namespace OnInit
    {
        ID_FORCEINLINE void IDLog_init() noexcept { ::ID::Log::init(); }

        // 在 Log/Log.cpp 里实现；返回 false 表示日志器未就绪
        bool IDWindow_logger_init() noexcept;

        // 在 WindowPool.cpp 里实现：复位槽存活位、自由链与 current
        void reset_window_pool() noexcept;
    } // namespace OnInit

    namespace OnShutdown
    {
        // 在 Log/Log.cpp 里实现：销毁本模块自己的日志器句柄并清空日志器指针。
        // `shutdown()` 与 `init()` 的平台初始化失败回滚都调用它，因此实现必须幂等
        void IDWindow_logger_shutdown() noexcept;

        // 在 WindowPool.cpp 里实现：销毁全部存活窗口并复位槽表与 current
        void destroy_window_pool() noexcept;
    } // namespace OnShutdown
} // namespace ID::Window::LifeCycle

namespace ID::Window::Detail
{
    /**
     *  @brief 写入本线程的错误文本缓冲
     *  @param text 诊断文本；`nullptr` 按空串处理
     *  @note 只做定长字节拷贝，不格式化、不取日志器：日志器未就绪时错误文本通道照常可用
     *  @note 实现见 `src/IDWindow.cpp`
     */
    void set_error_text(const char* text) noexcept;
} // namespace ID::Window::Detail
