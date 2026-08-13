#include <array>
#include <fstream>

#include "DevGUI/ImGui/ImGuiLayer.hpp"
#include "DevGUI/ImGui/Panels/MenuBarPanel.hpp"
#include "DevGUI/ImGui/Panels/SceneHierarchyPanel.hpp"
#include "DevGUI/ImGui/Panels/InspectorPanel.hpp"
#include "DevGUI/ImGui/Panels/PhysicsSettingsPanel.hpp"

#include "Scene/SceneManager.hpp"
#include "Log/Log.hpp"

#include "IDWindow.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

namespace ID
{
    namespace
    {
        // 编译期单点映射：仅用于生成下方查表 g_KeyMap，未知键返回 ImGuiKey_None
        consteval ImGuiKey to_imgui_key_impl(KeyCode key)
        {
            switch(key.get_value())
            {
                case KeyCodes::Space:         return ImGuiKey_Space;
                case KeyCodes::Apostrophe:    return ImGuiKey_Apostrophe;
                case KeyCodes::Comma:         return ImGuiKey_Comma;
                case KeyCodes::Minus:         return ImGuiKey_Minus;
                case KeyCodes::Period:        return ImGuiKey_Period;
                case KeyCodes::Slash:         return ImGuiKey_Slash;
                case KeyCodes::D0:            return ImGuiKey_0;
                case KeyCodes::D1:            return ImGuiKey_1;
                case KeyCodes::D2:            return ImGuiKey_2;
                case KeyCodes::D3:            return ImGuiKey_3;
                case KeyCodes::D4:            return ImGuiKey_4;
                case KeyCodes::D5:            return ImGuiKey_5;
                case KeyCodes::D6:            return ImGuiKey_6;
                case KeyCodes::D7:            return ImGuiKey_7;
                case KeyCodes::D8:            return ImGuiKey_8;
                case KeyCodes::D9:            return ImGuiKey_9;
                case KeyCodes::Semicolon:     return ImGuiKey_Semicolon;
                case KeyCodes::Equal:         return ImGuiKey_Equal;
                case KeyCodes::A:             return ImGuiKey_A;
                case KeyCodes::B:             return ImGuiKey_B;
                case KeyCodes::C:             return ImGuiKey_C;
                case KeyCodes::D:             return ImGuiKey_D;
                case KeyCodes::E:             return ImGuiKey_E;
                case KeyCodes::F:             return ImGuiKey_F;
                case KeyCodes::G:             return ImGuiKey_G;
                case KeyCodes::H:             return ImGuiKey_H;
                case KeyCodes::I:             return ImGuiKey_I;
                case KeyCodes::J:             return ImGuiKey_J;
                case KeyCodes::K:             return ImGuiKey_K;
                case KeyCodes::L:             return ImGuiKey_L;
                case KeyCodes::M:             return ImGuiKey_M;
                case KeyCodes::N:             return ImGuiKey_N;
                case KeyCodes::O:             return ImGuiKey_O;
                case KeyCodes::P:             return ImGuiKey_P;
                case KeyCodes::Q:             return ImGuiKey_Q;
                case KeyCodes::R:             return ImGuiKey_R;
                case KeyCodes::S:             return ImGuiKey_S;
                case KeyCodes::T:             return ImGuiKey_T;
                case KeyCodes::U:             return ImGuiKey_U;
                case KeyCodes::V:             return ImGuiKey_V;
                case KeyCodes::W:             return ImGuiKey_W;
                case KeyCodes::X:             return ImGuiKey_X;
                case KeyCodes::Y:             return ImGuiKey_Y;
                case KeyCodes::Z:             return ImGuiKey_Z;
                case KeyCodes::LeftBracket:   return ImGuiKey_LeftBracket;
                case KeyCodes::Backslash:     return ImGuiKey_Backslash;
                case KeyCodes::RightBracket:  return ImGuiKey_RightBracket;
                case KeyCodes::GraveAccent:   return ImGuiKey_GraveAccent;
                case KeyCodes::World1:        return ImGuiKey_Oem102;
                case KeyCodes::World2:        return ImGuiKey_Oem102;
                case KeyCodes::Escape:        return ImGuiKey_Escape;
                case KeyCodes::Enter:         return ImGuiKey_Enter;
                case KeyCodes::Tab:           return ImGuiKey_Tab;
                case KeyCodes::Backspace:     return ImGuiKey_Backspace;
                case KeyCodes::Insert:        return ImGuiKey_Insert;
                case KeyCodes::Delete:        return ImGuiKey_Delete;
                case KeyCodes::Right:         return ImGuiKey_RightArrow;
                case KeyCodes::Left:          return ImGuiKey_LeftArrow;
                case KeyCodes::Down:          return ImGuiKey_DownArrow;
                case KeyCodes::Up:            return ImGuiKey_UpArrow;
                case KeyCodes::PageUp:        return ImGuiKey_PageUp;
                case KeyCodes::PageDown:      return ImGuiKey_PageDown;
                case KeyCodes::Home:          return ImGuiKey_Home;
                case KeyCodes::End:           return ImGuiKey_End;
                case KeyCodes::CapsLock:      return ImGuiKey_CapsLock;
                case KeyCodes::ScrollLock:    return ImGuiKey_ScrollLock;
                case KeyCodes::NumLock:       return ImGuiKey_NumLock;
                case KeyCodes::PrintScreen:   return ImGuiKey_PrintScreen;
                case KeyCodes::Pause:         return ImGuiKey_Pause;
                case KeyCodes::F1:            return ImGuiKey_F1;
                case KeyCodes::F2:            return ImGuiKey_F2;
                case KeyCodes::F3:            return ImGuiKey_F3;
                case KeyCodes::F4:            return ImGuiKey_F4;
                case KeyCodes::F5:            return ImGuiKey_F5;
                case KeyCodes::F6:            return ImGuiKey_F6;
                case KeyCodes::F7:            return ImGuiKey_F7;
                case KeyCodes::F8:            return ImGuiKey_F8;
                case KeyCodes::F9:            return ImGuiKey_F9;
                case KeyCodes::F10:           return ImGuiKey_F10;
                case KeyCodes::F11:           return ImGuiKey_F11;
                case KeyCodes::F12:           return ImGuiKey_F12;
                case KeyCodes::KP0:           return ImGuiKey_Keypad0;
                case KeyCodes::KP1:           return ImGuiKey_Keypad1;
                case KeyCodes::KP2:           return ImGuiKey_Keypad2;
                case KeyCodes::KP3:           return ImGuiKey_Keypad3;
                case KeyCodes::KP4:           return ImGuiKey_Keypad4;
                case KeyCodes::KP5:           return ImGuiKey_Keypad5;
                case KeyCodes::KP6:           return ImGuiKey_Keypad6;
                case KeyCodes::KP7:           return ImGuiKey_Keypad7;
                case KeyCodes::KP8:           return ImGuiKey_Keypad8;
                case KeyCodes::KP9:           return ImGuiKey_Keypad9;
                case KeyCodes::KPDecimal:     return ImGuiKey_KeypadDecimal;
                case KeyCodes::KPDivide:      return ImGuiKey_KeypadDivide;
                case KeyCodes::KPMultiply:    return ImGuiKey_KeypadMultiply;
                case KeyCodes::KPSubtract:    return ImGuiKey_KeypadSubtract;
                case KeyCodes::KPAdd:         return ImGuiKey_KeypadAdd;
                case KeyCodes::KPEnter:       return ImGuiKey_KeypadEnter;
                case KeyCodes::KPEqual:       return ImGuiKey_KeypadEqual;
                case KeyCodes::LeftShift:     return ImGuiKey_LeftShift;
                case KeyCodes::LeftControl:   return ImGuiKey_LeftCtrl;
                case KeyCodes::LeftAlt:       return ImGuiKey_LeftAlt;
                case KeyCodes::LeftSuper:     return ImGuiKey_LeftSuper;
                case KeyCodes::RightShift:    return ImGuiKey_RightShift;
                case KeyCodes::RightControl:  return ImGuiKey_RightCtrl;
                case KeyCodes::RightAlt:      return ImGuiKey_RightAlt;
                case KeyCodes::RightSuper:    return ImGuiKey_RightSuper;
                case KeyCodes::Menu:          return ImGuiKey_Menu;
                default:                      return ImGuiKey_None;
            }
        }

        // KeyCode 取值 0~Last，编译期生成连续查表，未映射槽位默认 ImGuiKey_None
        constexpr std::size_t key_map_size = static_cast<std::size_t>(KeyCodes::Last.get_value()) + 1;
        constexpr std::array<ImGuiKey, key_map_size> g_KeyMap = []() constexpr {
            std::array<ImGuiKey, key_map_size> map{};
            for(KeyCodeType i = 0; i < static_cast<KeyCodeType>(key_map_size); ++i)
                map[static_cast<std::size_t>(i)] = to_imgui_key_impl(KeyCode{i});
            return map;
        }();

        /*
        *   to_imgui_key 将 KeyCode 转换为 ImGuiKey，运行时 O(1) 查表，封装 g_KeyMap。
        */
        inline ImGuiKey to_imgui_key(KeyCode key)
        {
            return g_KeyMap[static_cast<std::size_t>(key.get_value())];
        }

        /*
        *   加载中文字体（Windows 系统字体），保证 Console 等面板可显示中文日志。
        *   在 backend 首次 NewFrame 之前调用（backend 会自动 Build）。
        */
        void load_chinese_font(ImGuiIO& io)
        {
            const char* candidates[] = {
                "C:\\Windows\\Fonts\\msyh.ttc",
                "C:\\Windows\\Fonts\\simhei.ttf",
                "C:\\Windows\\Fonts\\msyh.ttf",
            };

            for(const char* path : candidates)
            {
                std::ifstream font_test(path, std::ios::binary);
                if(!font_test.good()) continue;
                font_test.close();

                ImFontConfig font_cfg;
                font_cfg.OversampleH = 1;
                font_cfg.OversampleV = 1;
                font_cfg.PixelSnapH  = true;
                io.Fonts->AddFontFromFileTTF(path, 16.0f, &font_cfg,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
                ID_INFO("[ImGuiLayer] 中文字体加载成功: {}", path);
                return;
            }

            ID_WARN("[ImGuiLayer] 未找到中文字体，中文内容可能显示为占位符");
        }

        /*
        *   GLFW 字符输入回调：IDWindow 事件系统未提供 KeyTyped 事件，
        *   此处单独安装 CharCallback 以支持 ImGui 文本框键入文字。
        */
        void glfw_char_callback(GLFWwindow* /*window*/, unsigned int codepoint)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddInputCharacter(codepoint);
        }
    } // 匿名命名空间

    // =====================================================================
    //  ImGuiLayer — 生命周期
    // =====================================================================

    ImGuiLayer::ImGuiLayer(const std::string& name) : Layer(name) { }

    ImGuiLayer::~ImGuiLayer() = default;

    void ImGuiLayer::on_attach()
    {
        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        load_chinese_font(io);

        // 手动事件转发模式：不安装 ImGui 的 GLFW 回调，
        // 事件由 ImGuiLayer::on_event() 统一转发，便于 WantCapture 阻断
        GLFWwindow* native_window = static_cast<GLFWwindow*>(WindowPool::get_native_handle());
        ImGui_ImplGlfw_InitForOpenGL(native_window, false);
        ImGui_ImplOpenGL3_Init("#version 330");

        // 字符输入（IDWindow 未占用 CharCallback，无冲突）
        glfwSetCharCallback(native_window, glfw_char_callback);

        m_window_width  = WindowPool::get_width();
        m_window_height = WindowPool::get_height();

        ID_INFO("[ImGuiLayer] on_attach 完成 ({} x {})", m_window_width, m_window_height);
    }

    void ImGuiLayer::on_detach()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;

        ID_INFO("[ImGuiLayer] on_detach 完成");
    }

    void ImGuiLayer::on_update(Timestep ts)
    {
        (void)ts;
        if(!m_context) return;

        begin_frame();

        render_dockspace();
        render_menu_bar();

        // 每帧向 Hierarchy / Inspector 注入场景与选中状态
        // （Hierarchy → Inspector 的选中状态经由本 Layer 中转）
        Scene* active_scene = get_active_scene();
        if(auto* hierarchy = get_panel<SceneHierarchyPanel>())
        {
            hierarchy->set_context(active_scene);
        }
        if(auto* inspector = get_panel<InspectorPanel>())
        {
            inspector->set_context(active_scene, m_selected_object);
        }
        if(auto* physics = get_panel<PhysicsSettingsPanel>())
        {
            physics->set_context(active_scene);
        }

        // 遍历所有 Panel 逐帧绘制
        for(auto& panel : m_panels)
        {
            if(panel->is_open())
            {
                panel->on_imgui_render();
            }
        }

        end_frame();
    }

    void ImGuiLayer::on_event(Event& event)
    {
        if(!m_context) return;

        // ---- 事件转发：IDWindow → ImGui IO ----
        EventDispatcher dispatcher(event);

        dispatcher.dispatch<KeyPressedEvent>([this](KeyPressedEvent& e)
        {
            handle_key_event(e);
            return false;   // 转发不消耗事件
        });
        dispatcher.dispatch<KeyReleasedEvent>([this](KeyReleasedEvent& e)
        {
            handle_key_event(e);
            return false;
        });
        dispatcher.dispatch<MouseMovedEvent>([this](MouseMovedEvent& e)
        {
            handle_mouse_event(e);
            return false;
        });
        dispatcher.dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& e)
        {
            handle_mouse_event(e);
            return false;
        });
        dispatcher.dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& e)
        {
            handle_mouse_event(e);
            return false;
        });
        dispatcher.dispatch<MouseButtonReleasedEvent>([this](MouseButtonReleasedEvent& e)
        {
            handle_mouse_event(e);
            return false;
        });
        dispatcher.dispatch<WindowResizeEvent>([this](WindowResizeEvent& e)
        {
            handle_window_resize(e);
            return false;
        });

        // ---- WantCapture 阻断：ImGui 需要输入时，事件不再向下传播 ----
        ImGuiIO& io = ImGui::GetIO();
        const auto category = static_cast<uint8_t>(event.get_category());
        const auto keyboard = static_cast<uint8_t>(EventCategory::Keyboard);
        const auto mouse    = static_cast<uint8_t>(EventCategory::Mouse);

        if(io.WantCaptureKeyboard && (category & keyboard) != 0)
            event.set_handled(true);
        if(io.WantCaptureMouse && (category & mouse) != 0)
            event.set_handled(true);
    }

    // =====================================================================
    //  ImGuiLayer — 事件转发实现
    // =====================================================================

    void ImGuiLayer::handle_key_event(Event& event)
    {
        ImGuiIO& io = ImGui::GetIO();

        if(event.get_type() == EventType::KeyPressed)
        {
            const auto& key_event = static_cast<const KeyPressedEvent&>(event);
            const ImGuiKey key = to_imgui_key(key_event.get_key_code());
            if(key != ImGuiKey_None)
                io.AddKeyEvent(key, true);
        }
        else if(event.get_type() == EventType::KeyReleased)
        {
            const auto& key_event = static_cast<const KeyReleasedEvent&>(event);
            const ImGuiKey key = to_imgui_key(key_event.get_key_code());
            if(key != ImGuiKey_None)
                io.AddKeyEvent(key, false);
        }

        // 手动转发模式：修饰键状态需自行同步
        io.AddKeyEvent(ImGuiMod_Ctrl,  Input::is_key_pressed(KeyCodes::LeftControl)
                                    || Input::is_key_pressed(KeyCodes::RightControl));
        io.AddKeyEvent(ImGuiMod_Shift, Input::is_key_pressed(KeyCodes::LeftShift)
                                    || Input::is_key_pressed(KeyCodes::RightShift));
        io.AddKeyEvent(ImGuiMod_Alt,   Input::is_key_pressed(KeyCodes::LeftAlt)
                                    || Input::is_key_pressed(KeyCodes::RightAlt));
        io.AddKeyEvent(ImGuiMod_Super, Input::is_key_pressed(KeyCodes::LeftSuper)
                                    || Input::is_key_pressed(KeyCodes::RightSuper));
    }

    void ImGuiLayer::handle_mouse_event(Event& event)
    {
        ImGuiIO& io = ImGui::GetIO();

        switch (event.get_type())
        {
            case EventType::MouseMoved:
            {
                const auto& mouse_event = static_cast<const MouseMovedEvent&>(event);
                io.AddMousePosEvent(mouse_event.get_x(), mouse_event.get_y());
                break;
            }
            case EventType::MouseScrolled:
            {
                const auto& mouse_event = static_cast<const MouseScrolledEvent&>(event);
                io.AddMouseWheelEvent(mouse_event.get_x_offset(), mouse_event.get_y_offset());
                break;
            }
            case EventType::MouseButtonPressed:
            {
                const auto& mouse_event = static_cast<const MouseButtonPressedEvent&>(event);
                io.AddMouseButtonEvent(mouse_event.get_button(), true);
                break;
            }
            case EventType::MouseButtonReleased:
            {
                const auto& mouse_event = static_cast<const MouseButtonReleasedEvent&>(event);
                io.AddMouseButtonEvent(mouse_event.get_button(), false);
                break;
            }
            default:
                break;
        }
    }

    void ImGuiLayer::handle_window_resize(const WindowResizeEvent& event)
    {
        m_window_width  = event.get_width();
        m_window_height = event.get_height();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_window_width),
                                static_cast<float>(m_window_height));
    }

    // =====================================================================
    //  ImGuiLayer — 每帧绘制
    // =====================================================================

    void ImGuiLayer::begin_frame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_window_width),
                                static_cast<float>(m_window_height));

        ImGui::NewFrame();
    }

    void ImGuiLayer::end_frame()
    {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiLayer::render_dockspace()
    {
        // 全屏 DockSpace：作为所有 Panel 窗口的宿主，用户可自由拖拽/停靠面板。
        // DockSpaceOverViewport 自动使用主视口的工作区（WorkPos/WorkSize），
        // 因此与顶部 MenuBar 不重叠。
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    }

    void ImGuiLayer::render_menu_bar()
    {
        MenuBarPanel* menu_bar = get_panel<MenuBarPanel>();
        if(!menu_bar) return;

        // 同步面板指针列表（View 菜单需要 toggle 各 Panel 的可见性）
        std::vector<ImGuiPanel*> panels;
        panels.reserve(m_panels.size());
        for(auto& panel : m_panels)
        {
            panels.push_back(panel.get());
        }
        menu_bar->set_panels(std::move(panels));

        menu_bar->on_imgui_render();
    }

    Scene* ImGuiLayer::get_active_scene() const
    {
        return &SceneManager::get_current_scene();
    }
} // namespace ID
