#pragma once

#include "Layer/Layer.hpp"
#include "Scene/GameObject.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"
#include "Events/WindowEvent.hpp"

#include <memory>
#include <vector>

namespace ID
{
    class Scene;

    /**
     *  ImGuiLayer — DevGUI 主 Layer（overlay）
     *
     *  职责：
     *  - ImGui Context 生命周期管理（CreateContext / DestroyContext）
     *  - IDWindow 事件转发（Key / Mouse / WindowResize）→ ImGui IO
     *  - WantCaptureKeyboard / WantCaptureMouse 阻断逻辑
     *  - Panel 管理器：注册 / 查找 / 逐帧渲染
     *  - 全局选中状态（Hierarchy → Inspector 通信的唯一中介）
     */
    class ID_API ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer(const std::string& name = "ImGuiLayer");
        ~ImGuiLayer() override;

        void on_attach() override;
        void on_detach() override;
        void on_update(Timestep ts) override;
        void on_event(Event& event) override;

        // ---- Panel 注册接口 ----
        template<typename PanelType, typename... Args>
        PanelType& add_panel(Args&&... args);

        template<typename PanelType>
        PanelType* get_panel();

        // ---- 全局选中状态（供 Hierarchy → Inspector 通信）----
        void            set_selected_object(GameObject::ID id) { m_selected_object = id; }
        GameObject::ID  get_selected_object() const { return m_selected_object; }
        Scene*          get_active_scene() const;

    private:
        // ImGui 事件转发
        void handle_key_event(Event& event);
        void handle_mouse_event(Event& event);
        void handle_window_resize(const WindowResizeEvent& event);

        // 鼠标是否悬停在「不参与鼠标捕获」的面板（如 Viewport）上
        bool is_mouse_over_capture_exempt_panel() const;

        // 每帧绘制
        void begin_frame();
        void end_frame();
        void render_dockspace();
        void render_menu_bar();

    private:
        std::vector<std::unique_ptr<ImGuiPanel>> m_panels;

        GameObject::ID m_selected_object = GameObject::INVALID_ID;

        uint32_t m_window_width  = 0;
        uint32_t m_window_height = 0;

        // ImGui 上下文
        ImGuiContext* m_context = nullptr;
    };

    // ---- 模板实现（必须在头文件中）----

    /**
     *  @brief 注册一个新的 Panel 到 ImGuiLayer
     *  @return 新 Panel 的引用（PanelType 需继承 ImGuiPanel）
     */
    template<typename PanelType, typename... Args>
    PanelType& ImGuiLayer::add_panel(Args&&... args)
    {
        static_assert(std::is_base_of<ImGuiPanel, PanelType>::value,
            "add_panel 传入的类型必须是 ImGuiPanel 的子类");

        auto panel = std::make_unique<PanelType>(std::forward<Args>(args)...);
        PanelType& ref = *panel;
        m_panels.push_back(std::move(panel));
        return ref;
    }

    /**
     *  @brief 按类型查找已注册的 Panel
     *  @return 找到返回指针，未找到返回 nullptr
     */
    template<typename PanelType>
    PanelType* ImGuiLayer::get_panel()
    {
        static_assert(std::is_base_of<ImGuiPanel, PanelType>::value,
            "get_panel 传入的类型必须是 ImGuiPanel 的子类");

        for(auto& p : m_panels)
        {
            auto* casted = dynamic_cast<PanelType*>(p.get());
            if(casted) return casted;
        }
        return nullptr;
    }
} // namespace ID
