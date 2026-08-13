#include "Renderer/Camera/CameraController.hpp"

#include "Events/EventDispatcher.hpp"
#include "Input/Input.hpp"

#include <cmath>

namespace ID
{
    namespace
    {
        constexpr int   ROTATE_BUTTON      = 0;     // 0 = GLFW_MOUSE_BUTTON_1（左键）
        constexpr float SPIKE_THRESHOLD    = 200.0f; // 鼠标 delta 尖峰阈值（像素）
    }

    // =====================================================================
    //  事件分发 — 状态机入口
    // =====================================================================

    void FreeLookCameraController::on_event(Event& event)
    {
        EventDispatcher dispatcher(event);

        dispatcher.dispatch<KeyPressedEvent>([this](KeyPressedEvent& e)
        {
            return handle_key_pressed(e);
        });
        dispatcher.dispatch<KeyReleasedEvent>([this](KeyReleasedEvent& e)
        {
            return handle_key_released(e);
        });
        dispatcher.dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& e)
        {
            return handle_mouse_button_pressed(e);
        });
        dispatcher.dispatch<MouseButtonReleasedEvent>([this](MouseButtonReleasedEvent& e)
        {
            return handle_mouse_button_released(e);
        });
        dispatcher.dispatch<MouseMovedEvent>([this](MouseMovedEvent& e)
        {
            return handle_mouse_moved(e);
        });
    }

    bool FreeLookCameraController::handle_key_pressed(const KeyPressedEvent& e)
    {
        const KeyCode key = e.get_key_code();

        if(key == KeyCodes::W) { m_key_w = true; return true; }
        if(key == KeyCodes::S) { m_key_s = true; return true; }
        if(key == KeyCodes::A) { m_key_a = true; return true; }
        if(key == KeyCodes::D) { m_key_d = true; return true; }

        return false;   // 非相机按键不消费，继续向下传播
    }

    bool FreeLookCameraController::handle_key_released(const KeyReleasedEvent& e)
    {
        const KeyCode key = e.get_key_code();

        if(key == KeyCodes::W) { m_key_w = false; return true; }
        if(key == KeyCodes::S) { m_key_s = false; return true; }
        if(key == KeyCodes::A) { m_key_a = false; return true; }
        if(key == KeyCodes::D) { m_key_d = false; return true; }

        return false;
    }

    bool FreeLookCameraController::handle_mouse_button_pressed(const MouseButtonPressedEvent& e)
    {
        if(e.get_button() != ROTATE_BUTTON) return false;   // 相机只认左键

        // MouseButtonPressedEvent 不携带坐标：按下时用轮询坐标重置锚点，
        // 消除「上一次旋转留下的陈旧锚点」造成的首次 delta 跳变。
        // （仅此一次点查询，不构成持续轮询）
        auto [x, y] = Input::get_mouse_position();
        m_last_mouse_x = x;
        m_last_mouse_y = y;
        m_rotating = true;
        return true;   // 左键按下归相机消费（能到达这里说明不在 ImGui 捕获区）
    }

    bool FreeLookCameraController::handle_mouse_button_released(const MouseButtonReleasedEvent& e)
    {
        if(e.get_button() != ROTATE_BUTTON) return false;

        m_rotating = false;
        return true;
    }

    bool FreeLookCameraController::handle_mouse_moved(const MouseMovedEvent& e)
    {
        if(!m_rotating)
        {
            // 未旋转时也跟踪锚点，保证下次按下时锚点已是最新
            m_last_mouse_x = e.get_x();
            m_last_mouse_y = e.get_y();
            return false;   // 未旋转不消费移动事件
        }

        const float dx = e.get_x() - m_last_mouse_x;
        const float dy = e.get_y() - m_last_mouse_y;
        m_last_mouse_x = e.get_x();
        m_last_mouse_y = e.get_y();

        // 尖峰保护：拖拽途中光标若扫过 Panel，MouseMoved 被 ImGui 吞掉一段时间，
        // 恢复时 dx/dy 会异常巨大。超过阈值视为「不连续」，只重置锚点不旋转。
        if(std::abs(dx) > SPIKE_THRESHOLD || std::abs(dy) > SPIKE_THRESHOLD)
        {
            return true;   // 吞掉尖峰，避免相机猛转
        }

        apply_rotation(dx, dy);
        return true;   // 旋转中消费移动事件
    }

    // =====================================================================
    //  运动计算
    // =====================================================================

    void FreeLookCameraController::apply_rotation(float dx, float dy)
    {
        CameraPose pose = m_camera.get_pose();

        const float yaw   = -dx * m_mouse_sensitivity;
        const float pitch = -dy * m_mouse_sensitivity;

        // Yaw：绕世界 Y 轴旋转 front 和 up
        pose.front = rotate_around(pose.front, Vec3(0.0f, 1.0f, 0.0f), yaw);
        pose.up    = rotate_around(pose.up,    Vec3(0.0f, 1.0f, 0.0f), yaw);

        // Pitch：绕摄像机 right 轴旋转 front 和 up
        Vec3 right = Math::cross(pose.front, pose.up);
        pose.front = rotate_around(pose.front, right, pitch);
        pose.up    = rotate_around(pose.up,    right, pitch);

        pose.front.normalize();
        pose.up.normalize();

        m_camera.set_pose(pose);
    }

    void FreeLookCameraController::apply_movement(CameraPose& pose, Timestep ts)
    {
        const float speed = m_move_speed * ts.get_seconds();

        if(m_key_w) pose.position += pose.front * speed;
        if(m_key_s) pose.position -= pose.front * speed;
        if(m_key_a) pose.position -= pose.right() * speed;
        if(m_key_d) pose.position += pose.right() * speed;
    }

    // =====================================================================
    //  每帧更新 — 安全网对账 + 平移积分
    // =====================================================================

    void FreeLookCameraController::on_update(Timestep timestep)
    {
        // ── 1. 安全网：只清 0，不置 1（铁律 1 / 铁律 3）──
        reconcile_input_state();

        // ── 2. WASD 平移（状态位已由事件维护）──
        CameraPose pose = m_camera.get_pose();
        if(m_key_w || m_key_s || m_key_a || m_key_d)
        {
            apply_movement(pose, timestep);
        }

        m_camera.set_pose(pose);
    }

    void FreeLookCameraController::reconcile_input_state()
    {
        // 单方向对账：物理按键已松开（轮询 false）→ 状态位清 0。
        // 反向（轮询 true 而状态 false）绝不置位 ——
        // 激活必须由未被 ImGui 吞掉的事件触发，否则 Panel 上又能控制相机。
        if(m_key_w && !Input::is_key_pressed(KeyCodes::W)) m_key_w = false;
        if(m_key_s && !Input::is_key_pressed(KeyCodes::S)) m_key_s = false;
        if(m_key_a && !Input::is_key_pressed(KeyCodes::A)) m_key_a = false;
        if(m_key_d && !Input::is_key_pressed(KeyCodes::D)) m_key_d = false;

        if(m_rotating && !Input::is_mouse_button_pressed(ROTATE_BUTTON))
        {
            m_rotating = false;
        }
    }

    Vec3 FreeLookCameraController::rotate_around(const Vec3& v, const Vec3& axis, float angle_deg)
    {
        const float rad = angle_deg * 3.1415926535f / 180.0f;
        const float c = std::cos(rad);
        const float s = std::sin(rad);

        Vec3 k = axis;
        k.normalize();

        return v * c + Math::cross(k, v) * s + k * Math::dot(k, v) * (1.0f - c);
    }
} // namespace ID
