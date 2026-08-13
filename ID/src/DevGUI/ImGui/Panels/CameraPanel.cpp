#include "DevGUI/ImGui/Panels/CameraPanel.hpp"

namespace ID
{
    CameraPanel::CameraPanel() : ImGuiPanel("Camera", true) { }

    void CameraPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        if(!m_camera)
        {
            ImGui::TextDisabled("未注入相机（请通过 set_camera 设置）");
            end_window();
            return;
        }

        render_pose_editor(*m_camera);
        ImGui::Separator();
        render_projection_editor(*m_camera);

        end_window();
    }

    void CameraPanel::render_pose_editor(Camera& camera)
    {
        const CameraPose& pose = camera.get_pose();

        // Position
        float pos[3] = { pose.position[0], pose.position[1], pose.position[2] };
        if(ImGui::DragFloat3("Position", pos, 0.1f))
        {
            camera.set_position(Pos3(pos[0], pos[1], pos[2]));
        }

        // Front（前向向量）
        float front[3] = { pose.front[0], pose.front[1], pose.front[2] };
        if(ImGui::DragFloat3("Front", front, 0.01f))
        {
            Vec3 new_front(front[0], front[1], front[2]);
            new_front.normalize();
            camera.set_orientation(new_front, pose.up);
        }

        // Up（上向量）
        float up[3] = { pose.up[0], pose.up[1], pose.up[2] };
        if(ImGui::DragFloat3("Up", up, 0.01f))
        {
            Vec3 new_up(up[0], up[1], up[2]);
            new_up.normalize();
            camera.set_orientation(pose.front, new_up);
        }
    }

    void CameraPanel::render_projection_editor(Camera& camera)
    {
        ImGui::Text("Projection:");

        const ProjectionParams& projection = camera.get_projection();

        // 投影类型
        const char* type_names[] = { "Perspective", "Orthographic" };
        int type_index = (projection.type == ProjectionType::Perspective) ? 0 : 1;
        if(ImGui::Combo("Type", &type_index, type_names, 2))
        {
            camera.set_projection_type(type_index == 0
                ? ProjectionType::Perspective : ProjectionType::Orthographic);
        }

        // Near / Far（两种类型共用）
        float near_z = projection.near_z;
        float far_z  = projection.far_z;
        bool  near_far_changed = false;
        if(ImGui::DragFloat("Near", &near_z, 0.01f, 0.001f, far_z - 0.01f))
        {
            near_far_changed = true;
        }
        if(ImGui::DragFloat("Far", &far_z, 0.1f, near_z + 0.01f, 100000.0f))
        {
            near_far_changed = true;
        }

        // 类型相关参数
        if(projection.type == ProjectionType::Perspective)
        {
            float fov = projection.persp.fov_y;
            if(ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f, "%.1f deg"))
            {
                camera.set_perspective(fov, projection.persp.aspect);
                near_far_changed = false;   // set_perspective 不修改 near/far
            }

            ImGui::TextDisabled("Aspect: %.4f", projection.persp.aspect);
        }
        else
        {
            float left   = projection.ortho.left;
            float right  = projection.ortho.right;
            float bottom = projection.ortho.bottom;
            float top    = projection.ortho.top;
            if(ImGui::DragFloat("Left", &left, 0.1f) || ImGui::DragFloat("Right", &right, 0.1f)
                || ImGui::DragFloat("Bottom", &bottom, 0.1f) || ImGui::DragFloat("Top", &top, 0.1f))
            {
                camera.set_orthographic(left, right, top, bottom, near_z, far_z);
                near_far_changed = false;
            }
        }

        // Near / Far 变更（透视模式下 set_perspective 只改 fov，需单独写回）
        if(near_far_changed)
        {
            ProjectionParams params = projection;
            params.near_z = near_z;
            params.far_z  = far_z;
            camera.set_projection(params);
        }
    }
} // namespace ID
