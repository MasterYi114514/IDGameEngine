#pragma once

#include "Renderer/Camera/Camera.hpp"
#include "Events/Event.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
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

        /*
        *   事件入口，默认空实现。
        *   事件驱动型控制器重写此方法，由所属 Layer::on_event() 转发调用。
        */
        virtual void on_event(Event& event) { (void)event; }

    protected:
        Camera& m_camera;
    };

    // =====================================================================
    //  FreeLookCameraController  — 自由视角控制器（事件驱动版）
    //
    //      旋转：在非 ImGui 捕获区域按住鼠标左键拖拽（事件驱动）
    //            水平拖拽 → yaw（绕世界 Y 轴）
    //            垂直拖拽 → pitch（绕摄像机 right 轴）
    //      移动：W/S 沿 front 前后，A/D 沿 right 左右
    //            （按键状态由事件翻转，位移在 on_update 按帧积分）
    //      安全网：每帧与 Input 轮询对账（只清 0），修复被 ImGui 吞掉的释放事件
    // =====================================================================
    class ID_API FreeLookCameraController : public CameraController
    {
    public:
        explicit FreeLookCameraController(Camera& camera) : CameraController(camera) { }

        void on_update(Timestep timestep) override;
        void on_event(Event& event) override;

        void set_move_speed(float speed)       { m_move_speed = speed; }
        void set_mouse_sensitivity(float sens) { m_mouse_sensitivity = sens; }

        float get_move_speed()        const { return m_move_speed; }
        float get_mouse_sensitivity() const { return m_mouse_sensitivity; }

    private:
        // ---- 事件处理器（返回 true 表示消费该事件，阻断向下传播）----
        bool handle_key_pressed(const KeyPressedEvent& e);
        bool handle_key_released(const KeyReleasedEvent& e);
        bool handle_mouse_button_pressed(const MouseButtonPressedEvent& e);
        bool handle_mouse_button_released(const MouseButtonReleasedEvent& e);
        bool handle_mouse_moved(const MouseMovedEvent& e);

        // ---- 运动计算 ----
        void apply_rotation(float dx, float dy);        // 事件回调中调用
        void apply_movement(CameraPose& pose, Timestep ts);  // on_update 中调用

        // ---- 安全网：与物理输入状态对账（只清 0，不置 1）----
        void reconcile_input_state();

        // Rodrigues 旋转公式：向量 v 绕单位轴 k 旋转 angle 度
        static Vec3 rotate_around(const Vec3& v, const Vec3& axis, float angle_deg);

        float m_move_speed        = 5.0f;
        float m_mouse_sensitivity = 0.2f;   // 度/像素

        // ── 输入状态机（事件驱动翻转，轮询只纠错）──
        bool m_key_w = false;
        bool m_key_s = false;
        bool m_key_a = false;
        bool m_key_d = false;
        bool m_rotating = false;            // 左键旋转中

        float m_last_mouse_x = 0.0f;        // 旋转增量锚点
        float m_last_mouse_y = 0.0f;
    };

    // =====================================================================
    //  TrackballCameraController  — 轨道球控制器（TODO，本次不改动）
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
