#include "DevGUI/ImGui/Panels/PhysicsSettingsPanel.hpp"

#include "Scene/Scene.hpp"
#include "Log/Log.hpp"

namespace ID
{
    PhysicsSettingsPanel::PhysicsSettingsPanel() : ImGuiPanel("Physics Settings", true) { }

    void PhysicsSettingsPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        if(!m_context)
        {
            ImGui::TextDisabled("无活跃场景");
            end_window();
            return;
        }

        // ---- 重力 ----
        PhysicsSystem& physics = m_context->get_physics_system();
        Vec3 gravity = physics.get_gravity();

        float grav[3] = { gravity[0], gravity[1], gravity[2] };
        if(ImGui::DragFloat3("Gravity", grav, 0.05f, -100.0f, 100.0f))
        {
            physics.set_gravity(Vec3(grav[0], grav[1], grav[2]));
            ID_INFO("[PhysicsSettings] 重力已更新为 ({:.2f}, {:.2f}, {:.2f})",
                grav[0], grav[1], grav[2]);
        }

        ImGui::Separator();

        // ---- 模拟参数（展示；引擎当前在 PhysicsSystem::on_update 中使用默认值）----
        ImGui::Text("Simulation:");
        ImGui::TextDisabled("Fixed Timestep: 1/60 s");
        ImGui::TextDisabled("Max Sub Steps:  4");

        // ---- Pause Physics ----
        ImGui::Separator();
        ImGui::Text("Debug:");
        bool paused = !m_context->get_is_running();
        if(ImGui::Checkbox("Pause Physics", &paused))
        {
            if(paused)
            {
                m_context->set_paused();
                ID_INFO("[PhysicsSettings] 物理模拟已暂停");
            }
            else
            {
                m_context->set_running();
                ID_INFO("[PhysicsSettings] 物理模拟已恢复");
            }
        }
        ImGui::TextDisabled("(暂停整个场景更新，包括物理模拟)");

        end_window();
    }
} // namespace ID
