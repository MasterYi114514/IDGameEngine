#pragma once

#include "IDWindowCore.hpp"
#include "Window/WindowID.hpp"
#include "Window/WindowProps.hpp"

#include <functional>

namespace ID
{
    class Window;
    class Event;

    /*
    *   事件回调函数类型。
    */
    using EventCallbackFn = std::function<void(Event&)>;

    /*
    *   窗口池，管理所有窗口的创建、销毁和事件回调。
    */
    namespace WindowPool
    {
        /*
        *   创建一个窗口。
        *   @param props 窗口属性（标题、宽、高）
        *   @return 新窗口的 WindowID，若创建失败则返回无效 ID（127）
        */
        WindowID IDWINDOW_API create_window(const WindowProps& props);

        /*
        *   销毁指定 ID 的窗口，该槽位后续可被复用。
        *   若该窗口是当前窗口，则当前窗口置空。
        *   @param id 要销毁的 WindowID
        */
        void IDWINDOW_API destroy_window(const WindowID& id);

        /*
        *   获取当前窗口尺寸。
        */
        uint32_t IDWINDOW_API get_width();
        uint32_t IDWINDOW_API get_height();

        /*
        *   设置当前操作的窗口，后续操作均作用于该窗口。
        *   @param id 要切换到的 WindowID
        */
        void IDWINDOW_API set_current(const WindowID& id);

        /*
        *   驱动当前窗口的一帧：轮询事件 + 交换缓冲区。
        */
        void IDWINDOW_API on_update();

        /*
        *   检查当前窗口是否应关闭。
        *   @return 无当前窗口时返回 true
        */
        bool IDWINDOW_API should_close();

        /*
        *   获取当前窗口指针。
        *   @return 无当前窗口返回 nullptr
        */
        Window* IDWINDOW_API get_current();

        /*
        *   获取当前窗口的原生句柄（GLFW: GLFWwindow* / Win32: HWND）。
        *   @return 无当前窗口返回 nullptr
        */
        void* IDWINDOW_API get_native_handle();

        /*
        *   为指定窗口注册事件回调。
        *   @param id 目标 WindowID
        *   @param callback 事件回调函数
        */
        void IDWINDOW_API set_event_callback(const WindowID& id, const EventCallbackFn& callback);
    };
} // namespace ID
