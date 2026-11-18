#pragma once

#include "Core/Core.hpp"
#include "IDLog.hpp"

/**
 *  项目模块的生命周期头：日志插件初始化与项目日志器的初始化/清理声明。
 *
 *  本头是模块私有头，只在 `Log/Log.cpp` 与需要读级别的 `.cpp` 内包含，不进公开 include。
 *
 *  调用顺序固定：先 `LifeCycle::OnInit::IDLog_init()`，再 `LifeCycle::OnInit::IDProject_logger_init()`；
 *  后者返回 true 之后才允许调用任何项目日志宏。退出前调用
 *  `LifeCycle::OnShutdown::IDProject_logger_shutdown()`，返回后不得再调用日志宏。
 */

namespace ID::Project::LifeCycle
{
    namespace OnInit
    {
        ID_FORCEINLINE void IDLog_init() noexcept { ::ID::Log::init(); }

        // 在 Log/Log.cpp 里实现；返回 false 表示日志器未就绪（此时不得调用任何日志宏）
        bool IDProject_logger_init() noexcept;
    } // namespace OnInit

    namespace OnShutdown
    {
        // 在 Log/Log.cpp 里实现；幂等，未初始化时是无操作，返回后不得再调用日志宏
        void IDProject_logger_shutdown() noexcept;
    } // namespace OnShutdown
} // namespace ID::Project::LifeCycle
