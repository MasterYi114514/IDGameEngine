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
        WindowPool::set_current(m_window_id);

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
            // 计算帧时间
            auto current_time = clock::now();
            float delta = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;
            Timestep timestep(delta);

            WindowPool::on_update();

            // 更新所有 Layer
            for(Layer* layer : m_layer_stack)
            {
                layer->on_update(timestep);
            }
        }
    }

    void Application::on_event(Event& event)
    {
        switch(event.get_type())
        {
            case EventType::WindowClose:
            {
                event.set_handled(on_window_close());
                break;
            }
            default:
            {
                for(auto it = m_layer_stack.end(); it != m_layer_stack.begin(); )
                {
                    --it;
                    if(event.is_handled()) break;       // 对于已解决的事件，进行阻断传播
                    (*it)->on_event(event);
                }
            }
        }
    }

    bool Application::on_window_close()
    {
        m_running = false;
        return true;
    }
} // namespace ID
