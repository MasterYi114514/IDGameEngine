#include "Window/WindowPool.hpp"
#include "Window/Window.hpp"
#include "Input/Input.hpp"

#include "Log/Log.hpp"

namespace
{
    constexpr ID::WindowIDType INVALID_ID = 127;

    ID::Window*                                 g_CurrentWindow = nullptr;
    std::vector<std::unique_ptr<ID::Window>>    g_WindowPool;
    ID::EventCallbackFn                         g_EventCallback;

    /*
    *   向池中插入窗口，优先复用空闲槽位。
    *   @return 新窗口在池中的下标（即 WindowID），失败返回 INVALID_ID
    */
    size_t insert_window(const ID::WindowProps& props)
    {
        auto new_window = ID::Window::create(props);
        if(!new_window)
        {
            ID_WINDOW_ERROR("创建名为 '{}' 的窗口失败", props.title);
            return INVALID_ID;
        }

        // 扫描空闲槽位
        size_t new_id = g_WindowPool.size();
        for(size_t i = 0; i < g_WindowPool.size(); i++)
        {
            if(g_WindowPool[i]) continue;
            new_id = i;
            break;
        }

        if(new_id >= INVALID_ID)
        {
            ID_WINDOW_ERROR("窗口池已满（最多 127 个窗口），无法创建新窗口");
            return INVALID_ID;
        }

        if(new_id < g_WindowPool.size())
        {
            g_WindowPool[new_id] = std::move(new_window);
        }
        else
        {
            g_WindowPool.emplace_back(std::move(new_window));
        }

        ID_WINDOW_INFO("将名为 '{}' 的窗口添加到 WindowPool 中，分配的 ID 是 {}", props.title, new_id);
        return new_id;
    }

    /*
    *   从池中移除窗口（置空槽位，后续可复用）。
    */
    void pop_window(size_t id)
    {
        if(id >= g_WindowPool.size() || !g_WindowPool[id])
        {
            ID_WINDOW_ERROR("尝试销毁无效的窗口 ID: {}", id);
            return;
        }

        g_WindowPool[id].reset();
        ID_WINDOW_INFO("从 WindowPool 中销毁了 ID 为 {} 的窗口", id);
    }
} // 匿名命名空间

namespace ID
{
    WindowID create_window_id(WindowIDType id)
    {
        return ID::WindowID{ id };
    }

    WindowIDType WindowID::get_id() const
    {
        if(m_ID >= g_WindowPool.size() || !g_WindowPool[m_ID])
        {
            ID_WINDOW_ERROR("尝试获取无效的窗口 ID: {}", static_cast<int>(m_ID));
            return INVALID_ID;
        }
        return m_ID;
    }

    WindowID WindowPool::create_window(const WindowProps& props)
    {
        WindowIDType new_id = static_cast<WindowIDType>(insert_window(props));

        // 若为首个窗口，注入 Input 句柄并激活渲染上下文
        if(!g_CurrentWindow)
        {
            g_CurrentWindow = g_WindowPool[new_id].get();
            g_CurrentWindow->make_current();
            Input::set_native_window(g_CurrentWindow->get_native_handle());
        }

        // 若已注册全局回调，绑定到新窗口
        if(g_EventCallback)
        {
            g_WindowPool[new_id]->set_event_callback(g_EventCallback);
        }

        return create_window_id(new_id);
    }

    void WindowPool::destroy_window(const WindowID& id)
    {
        WindowIDType target = id.get_id();
        if(target == INVALID_ID) return;

        // 若销毁的是当前窗口，置空
        if(g_CurrentWindow && g_CurrentWindow == g_WindowPool[target].get())
        {
            g_CurrentWindow = nullptr;
        }

        pop_window(target);
    }

    uint32_t WindowPool::get_width()
    {
        if(!g_CurrentWindow) return 0;
        return g_CurrentWindow->get_width();
    }

    uint32_t WindowPool::get_height()
    {
        if(!g_CurrentWindow) return 0;
        return g_CurrentWindow->get_height();
    }

    void WindowPool::set_current(const WindowID& id)
    {
        WindowIDType target = id.get_id();
        if(target >= g_WindowPool.size() || !g_WindowPool[target])
        {
            ID_WINDOW_ERROR("尝试切换到无效的窗口 ID: {}", static_cast<int>(target));
            return;
        }

        g_CurrentWindow = g_WindowPool[target].get();
        g_CurrentWindow->make_current();
        Input::set_native_window(g_CurrentWindow->get_native_handle());

        ID_WINDOW_INFO("将 ID 为 {} 的窗口设置为当前操作的窗口", static_cast<int>(target));
    }

    void WindowPool::on_update()
    {
        if(!g_CurrentWindow)
        {
            ID_WINDOW_ERROR("没有设置当前操作的窗口，无法执行 on_update");
            return;
        }
        g_CurrentWindow->on_update();
    }

    bool WindowPool::should_close()
    {
        if(!g_CurrentWindow) return true;
        return false;   // 由调用者通过 WindowCloseEvent 自行管理
    }

    Window* WindowPool::get_current()
    {
        return g_CurrentWindow;
    }

    void* WindowPool::get_native_handle()
    {
        if(!g_CurrentWindow) return nullptr;
        return g_CurrentWindow->get_native_handle();
    }

    void WindowPool::set_event_callback(const WindowID& id, const EventCallbackFn& callback)
    {
        WindowIDType target = id.get_id();
        if(target >= g_WindowPool.size() || !g_WindowPool[target])
        {
            ID_WINDOW_ERROR("尝试为无效的窗口 ID 设置事件回调: {}", static_cast<int>(target));
            return;
        }
        g_WindowPool[target]->set_event_callback(callback);
    }
} // namespace ID
