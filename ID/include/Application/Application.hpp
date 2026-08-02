#pragma once

#include "IDpch.hpp"

#include <Application/Timestep.hpp>
#include <Events/Event.hpp>
#include <Events/WindowEvent.hpp>
#include <Window/WindowPool.hpp>
#include <Layer/Layer.hpp>
#include <Layer/LayerStack.hpp>

#include <memory>

namespace ID
{
    class ID_API Application
    {
    public:
        Application(const std::string& title  = "ID Game Engine",
                    uint32_t            width  = 1280,
                    uint32_t            height = 720);
        virtual ~Application();

        // 禁止拷贝
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void run();

        // ---- 图层管理 ----
        void push_layer(Layer* layer)   { m_layer_stack.push_layer(layer);   layer->on_attach(); }
        void push_overlay(Layer* layer) { m_layer_stack.push_overlay(layer); layer->on_attach(); }
        void pop_layer(Layer* layer)    { m_layer_stack.pop_layer(layer);    layer->on_detach(); }
        void pop_overlay(Layer* layer)  { m_layer_stack.pop_overlay(layer);  layer->on_detach(); }

        // ---- 窗口信息 ----
        uint32_t get_width()  const { return WindowPool::get_width();  }
        uint32_t get_height() const { return WindowPool::get_height(); }

        // ---- 单例（方便 Input 等系统访问 Application）----
        static Application& get_instance() { return *s_instance; }

    private:
        void on_event(Event& event);
        bool on_window_close(WindowCloseEvent& event);
        bool on_window_resize(WindowResizeEvent& event);

    private:
        WindowID                m_window_id;
        LayerStack              m_layer_stack;
        bool                    m_running = true;
        Timestep                m_last_frame_time;

        static Application*     s_instance;
    };

    // 由 Sandbox 实现
    Application* create_application();
} // namespace ID
