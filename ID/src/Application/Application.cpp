#include <Log/Log.hpp>
#include <Application/Application.hpp>
#include <Render/RenderCommand.hpp>

#include <Events/WindowEvent.hpp>

#include <chrono>

namespace ID
{
    Application* Application::s_instance = nullptr;

    // =====================================================================
    //  构造 / 析构
    // =====================================================================
    Application::Application(const std::string& title,
                             uint32_t width, uint32_t height)
    {
        if(s_instance)
        {
            ID_ERROR("Application 已有实例");
            std::exit(-1);
        }
        s_instance = this;

        // 创建窗口并注册事件回调
        m_window_id = WindowPool::create_window(
            WindowProps{ title, width, height }
        );
        WindowPool::set_event_callback(m_window_id, [this](Event& event)
        {
            on_event(event);
        });

        ID_INFO("Application 创建完成 ({} x {})", width, height);
    }

    Application::~Application()
    {
        s_instance = nullptr;
    }

    // =====================================================================
    //  run  — 主循环
    // =====================================================================
    void Application::run()
    {
        using clock = std::chrono::high_resolution_clock;

        auto last_time = clock::now();

        while(m_running)
        {
            // ---- 1. 计算帧时间 ----
            auto current_time = clock::now();
            float delta = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;
            Timestep timestep(delta);

            // ---- 2. 轮询事件（内部触发回调 → on_event → 分发给 Layer）----
            WindowPool::on_update();

            // ---- 3. 更新所有 Layer（从上到下）----
            for(Layer* layer : m_layer_stack)
            {
                layer->on_update(timestep);
            }
        }
    }

    void Application::on_event(Event& event)
    {
        EventDispatcher dispatcher(event);
        dispatcher.dispatch<WindowCloseEvent>([this](WindowCloseEvent& e)
        {
            return on_window_close(e);
        });
        dispatcher.dispatch<WindowResizeEvent>([this](WindowResizeEvent& e)
        {
            return on_window_resize(e);
        });

        // 再分发给 Layer（从栈顶开始，最上层优先）
        // 使用反向迭代器：栈顶在 vector 末尾
        for(auto it = m_layer_stack.end(); it != m_layer_stack.begin(); )
        {
            --it;
            if(event.is_handled()) break;
            (*it)->on_event(event);
        }
    }

    bool Application::on_window_close(WindowCloseEvent& event)
    {
        m_running = false;
        return true;
    }

    bool Application::on_window_resize(WindowResizeEvent& event)
    {
        RenderCommand::set_viewport(0, 0, event.get_width(), event.get_height());
        return false;   // 让 Layer 也能处理
    }
} // namespace ID
