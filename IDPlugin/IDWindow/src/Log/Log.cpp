#include "Log/Log.hpp"

#include "IDWindowLifeCycle.hpp"

/**
 *  IDWindow 模块日志器：定义唯一一份指针、创建/销毁本模块自己的日志器句柄。
 *
 *  不变量：
 *  1. `IDWindow_logger` 只在本文件定义，初值 `nullptr`；`extern` 声明在 `Log/Log.hpp`。
 *  2. 句柄与指针的有效期只覆盖本模块 `init()` 到 `shutdown()` 这一轮生命周期：
 *     成功路径缓存指针、`shutdown()` 里按句柄销毁并把指针置空。
 *  3. 失败分支直接返回 `false`，其中不调用任何日志宏（此时日志器可能未就绪）。
 */
namespace ID::Window
{
    Log::Logger* IDWindow_logger = nullptr;

    namespace
    {
        ::ID::Log::LoggerHandle g_logger_handle = {};     // 本模块自己的日志器句柄；无效表示尚未创建
    } // namespace

    namespace LifeCycle::OnInit
    {
        bool IDWindow_logger_init() noexcept
        {
            const ::ID::Log::LoggerHandle logger_handle = ::ID::Log::create_logger("IDWindow");
            if (!logger_handle.is_valid())
            {
                return false;
            }

            IDWindow_logger = ::ID::Log::get_logger(logger_handle);
            if (IDWindow_logger == nullptr)
            {
                ::ID::Log::destroy_logger(logger_handle);
                return false;
            }

            g_logger_handle = logger_handle;

            IDWINDOW_LOG_INFO("IDWindow_logger_init: Logger 创建成功");
            return true;
        }
    } // namespace LifeCycle::OnInit

    namespace LifeCycle::OnShutdown
    {
        void IDWindow_logger_shutdown() noexcept
        {
            if (g_logger_handle.is_valid())
            {
                ::ID::Log::destroy_logger(g_logger_handle);
                g_logger_handle = {};
            }

            IDWindow_logger = nullptr;
        }
    } // namespace LifeCycle::OnShutdown
} // namespace ID::Window
