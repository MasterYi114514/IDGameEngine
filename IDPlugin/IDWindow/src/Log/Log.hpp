#pragma once

#include <utility>

#include "IDLog.hpp"

/**
 *  IDWindow 的模块私有日志封装：日志器声明与全部日志宏（不进公开 include）。
 *
 *  不变量：
 *  1. 整个模块只有一个日志器指针，由 `LifeCycle::OnInit::IDWindow_logger_init()` 写入；
 *     就绪后只读，不传给其它模块。
 *  2. 六个级别宏与断言宏是唯一入口：格式串与参数原样转发，宏体不做格式化、不做级别判断。
 *  3. 取用前判空：日志器未就绪（`init()` 失败或已 `shutdown()`）时静默丢弃该条，
 *     既不影响返回值，也不影响错误文本通道。
 *  4. 格式串仍是调用点的字面量：转换在宏展开处完成，编译期格式检查照旧生效。
 *  5. 热路径不写日志：事件转发、窗口操作与查询、GL 交接都不调用本宏族；诊断只落在失败冷路径。
 */
namespace ID::Window
{
    extern Log::Logger* IDWindow_logger;

    namespace Detail
    {
        template <typename... Args>
        void log_trace(fmt::format_string<Args...> format, Args&&... args)
        {
            if (IDWindow_logger != nullptr) [[likely]]
            {
                IDWindow_logger->trace(format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void log_debug(fmt::format_string<Args...> format, Args&&... args)
        {
            if (IDWindow_logger != nullptr) [[likely]]
            {
                IDWindow_logger->debug(format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void log_info(fmt::format_string<Args...> format, Args&&... args)
        {
            if (IDWindow_logger != nullptr) [[likely]]
            {
                IDWindow_logger->info(format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void log_warn(fmt::format_string<Args...> format, Args&&... args)
        {
            if (IDWindow_logger != nullptr) [[likely]]
            {
                IDWindow_logger->warn(format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void log_error(fmt::format_string<Args...> format, Args&&... args)
        {
            if (IDWindow_logger != nullptr) [[likely]]
            {
                IDWindow_logger->error(format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void log_fatal(fmt::format_string<Args...> format, Args&&... args)
        {
            if (IDWindow_logger != nullptr) [[likely]]
            {
                IDWindow_logger->fatal(format, std::forward<Args>(args)...);
            }
        }
    } // namespace Detail
} // namespace ID::Window

#define IDWINDOW_LOG_TRACE(...)   ::ID::Window::Detail::log_trace(__VA_ARGS__)
#define IDWINDOW_LOG_DEBUG(...)   ::ID::Window::Detail::log_debug(__VA_ARGS__)
#define IDWINDOW_LOG_INFO(...)    ::ID::Window::Detail::log_info(__VA_ARGS__)
#define IDWINDOW_LOG_WARN(...)    ::ID::Window::Detail::log_warn(__VA_ARGS__)
#define IDWINDOW_LOG_ERROR(...)   ::ID::Window::Detail::log_error(__VA_ARGS__)
#define IDWINDOW_LOG_FATAL(...)   ::ID::Window::Detail::log_fatal(__VA_ARGS__)

#ifdef IDWINDOW_DEBUG
    #define IDWINDOW_LOG_ASSERT(cond, ...) do { if (!(cond)) { ::ID::Window::Detail::log_error(__VA_ARGS__); } } while(0)
#else
    #define IDWINDOW_LOG_ASSERT(cond, ...)
#endif
