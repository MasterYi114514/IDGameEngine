#include "DispatchScope.hpp"

#include <cstddef>

#include "IDWindowLifeCycle.hpp"
#include "Log/Log.hpp"

/**
 *  派发深度与"回调栈内违约"判定：本模块唯一的派发深度持有者。
 *
 *  不变量：
 *  1. 深度是每线程的 TU 私有状态（本文件匿名命名空间里的 `thread_local`）：不交出地址、
 *     不跨 TU 可见，其它翻译单元只经 `Detail` 下的函数访问，不直接读深度本身。它的语义是
 *     "调用线程自己是否在事件回调栈内"，因此不共享：任意线程调用 `wake_event_loop()` 时不与
 *     主线程的写构成数据竞争，也不会被主线程的回调栈误判。
 *  2. 进入与退出必须成对配平：唯一派发点 `Window::on_event` 用一个作用域对象配对调用
 *     `enter_dispatch()` / `leave_dispatch()`，一次派发只加一、返回必定减一。
 *  3. 深度只用于识别"在事件回调栈内调用禁用接口"这一种违约（计划书 §7.5），
 *     不参与事件转发判定，也不决定事件是否投递。
 *  4. 违约只写日志与错误文本，不改任何现态：调用方据 `reject_if_in_callback()` 的返回值直接返回。
 *  5. 违约判定在正常调用下只有一次比较与返回；诊断集中在 `ID_NOINLINE` + `ID_COLD` 冷函数里。
 */
namespace ID::Window::Detail
{
    namespace
    {
        constexpr std::size_t kReentrantTextCapacity = 160;     // 错误文本定长缓冲：前缀 + 函数名 + 后缀

        // 本线程的派发栈深度；0 表示本线程不在任何事件回调栈内。
        // 用 thread_local 而不是 atomic 或锁：跨线程本来就不该共享该判定，读写都在本线程完成。
        thread_local std::size_t g_dispatch_depth = 0;

        // 错误文本通道只接受一段文本、不做格式化，因此违约文本在这里按定长字节拼接
        std::size_t append_text(char* buffer, std::size_t length, const char* text) noexcept
        {
            for (std::size_t index = 0; text[index] != '\0' && length + 1 < kReentrantTextCapacity; ++index)
            {
                buffer[length] = text[index];
                ++length;
            }
            return length;
        }

        ID_NOINLINE ID_COLD void report_reentrant_call(const char* function_name) noexcept
        {
            char        text[kReentrantTextCapacity] = {};
            std::size_t length = append_text(text, 0, "事件回调栈内调用禁用接口（");
            if (function_name != nullptr)
            {
                length = append_text(text, length, function_name);
            }
            length = append_text(text, length, "）：本次调用被拒绝且不改变任何状态");
            text[length] = '\0';

            ::ID::Window::Detail::set_error_text(text);

            // 名字可能为空（接口允许）：此时换成不带名字的固定文案，不把 nullptr 交给格式化
            if (function_name != nullptr)
            {
                IDWINDOW_LOG_WARN("{}: 在事件回调栈内调用该接口属调用方违约，本次调用被拒绝且不改变任何状态", function_name);
            }
            else
            {
                IDWINDOW_LOG_WARN("在事件回调栈内调用禁用接口属调用方违约，本次调用被拒绝且不改变任何状态");
            }
        }

        // 派发深度配平被破坏才进来：移出热路径，`leave_dispatch` 只剩一次比较与自减
        ID_NOINLINE ID_COLD void report_depth_underflow() noexcept
        {
            IDWINDOW_LOG_ASSERT(false, "Detail::leave_dispatch: 派发深度为 0，进入与退出未成对配平");
        }
    } // namespace

    void enter_dispatch() noexcept
    {
        ++g_dispatch_depth;
    }

    void leave_dispatch() noexcept
    {
        if (g_dispatch_depth == 0) [[unlikely]]
        {
            report_depth_underflow();
            return;
        }

        --g_dispatch_depth;
    }

    bool reject_if_in_callback(const char* function_name) noexcept
    {
        if (g_dispatch_depth == 0)
        {
            return false;
        }

        report_reentrant_call(function_name);
        return true;
    }
} // namespace ID::Window::Detail
