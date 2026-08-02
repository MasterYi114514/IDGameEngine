#pragma once

#include <cmath>

#include "Camera/Camera.hpp"
#include "Input/Input.hpp"
#include "Input/KeyCode.hpp"

namespace ID
{
    enum class CameraControllerType
    {
        None,
        Trackball,
        FreeLook,
    };

    // =====================================================================
    //  CameraController  — 摄像机控制器基类
    // =====================================================================
    class ID_API CameraController
    {
    public:
        CameraController() = delete;
        explicit CameraController(Camera& camera) : m_camera(camera) { }
        virtual ~CameraController() = default;

        virtual void on_update(Timestep timestep) = 0;

    protected:
        Camera& m_camera;
    };

    // =====================================================================
    //  FreeLookCameraController  — 自由视角控制器
    //
    //      移动：W/S  沿 front 前后
    //            A/D  沿 right 左右
    //      旋转：按住鼠标左键拖拽
    //            水平拖拽 → yaw（绕世界 Y 轴）
    //            垂直拖拽 → pitch（绕摄像机 right 轴）
    // =====================================================================
    class ID_API FreeLookCameraController : public CameraController
    {
    public:
        explicit FreeLookCameraController(Camera& camera)
            : CameraController(camera)
        {
            auto [x, y] = Input::get_mouse_position();
            m_last_mouse_x = x;
            m_last_mouse_y = y;
        }

        void on_update(Timestep timestep) override
        {
            CameraPose pose = m_camera.get_pose();

            // ── 1. 鼠标旋转 ──
            auto [mx, my] = Input::get_mouse_position();
            float dx = mx - m_last_mouse_x;
            float dy = my - m_last_mouse_y;
            m_last_mouse_x = mx;
            m_last_mouse_y = my;

            if (Input::is_mouse_button_pressed(0))   // 0 = GLFW_MOUSE_BUTTON_1（左键）
            {
                float yaw   = -dx * m_mouse_sensitivity;
                float pitch = -dy * m_mouse_sensitivity;

                // Yaw：绕世界 Y 轴旋转 front 和 up
                pose.front = rotate_around(pose.front, Vec3(0.0f, 1.0f, 0.0f), yaw);
                pose.up    = rotate_around(pose.up,    Vec3(0.0f, 1.0f, 0.0f), yaw);

                // Pitch：绕摄像机 right 轴旋转 front 和 up
                Vec3 right = Math::cross(pose.front, pose.up);
                pose.front = rotate_around(pose.front, right, pitch);
                pose.up    = rotate_around(pose.up,    right, pitch);

                pose.front.normalize();
                pose.up.normalize();
            }

            // ── 2. WASD 移动 ──
            float speed = m_move_speed * timestep.get_seconds();
            bool moved = false;

            if (Input::is_key_pressed(KeyCodes::W))
            {
                pose.position += pose.front * speed;
                moved = true;
            }
            if (Input::is_key_pressed(KeyCodes::S))
            {
                pose.position -= pose.front * speed;
                moved = true;
            }
            if (Input::is_key_pressed(KeyCodes::A))
            {
                pose.position -= pose.right() * speed;
                moved = true;
            }
            if (Input::is_key_pressed(KeyCodes::D))
            {
                pose.position += pose.right() * speed;
                moved = true;
            }

            // ── 3. 应用 ──
            // 旋转总是生效（即使没移动也需要更新朝向），移动按需
            m_camera.set_pose(pose);
        }

        void set_move_speed(float speed)       { m_move_speed = speed; }
        void set_mouse_sensitivity(float sens) { m_mouse_sensitivity = sens; }

        float get_move_speed()        const { return m_move_speed; }
        float get_mouse_sensitivity() const { return m_mouse_sensitivity; }

    private:
        // Rodrigues 旋转公式：向量 v 绕单位轴 k 旋转 angle 度
        static Vec3 rotate_around(const Vec3& v, const Vec3& axis, float angle_deg)
        {
            float rad = angle_deg * 3.1415926535f / 180.0f;
            float c = std::cos(rad);
            float s = std::sin(rad);

            Vec3 k = axis;
            k.normalize();

            return v * c + Math::cross(k, v) * s + k * Math::dot(k, v) * (1.0f - c);
        }

        float m_move_speed       = 5.0f;
        float m_mouse_sensitivity = 0.2f;   // 度/像素

        float m_last_mouse_x = 0.0f;
        float m_last_mouse_y = 0.0f;
    };

    // =====================================================================
    //  TrackballCameraController  — 轨道球控制器（TODO）
    // =====================================================================
    class ID_API TrackballCameraController : public CameraController
    {
    public:
        explicit TrackballCameraController(Camera& camera)
            : CameraController(camera) { }

        void on_update(Timestep timestep) override { }

    private:
        static constexpr float ROTATION_SPEED = 0.5f;
    };
} // namespace ID
