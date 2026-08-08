#pragma once

#include "IDMath.hpp"

#include "Scene/Component/Component.hpp"
#include "Renderer/Pose.hpp"

namespace ID
{
    using ComponentPose = Pose<OrientationDescType::Quaternion>;

    class ID_API TransformComponent : public Component
    {
    public:
        TransformComponent();
        virtual ~TransformComponent() override = default;

    public:
        // Pose 相关 getter 和 setter
        const ComponentPose&    get_pose() const { return m_pose; }
        const Pos3&             get_position() const { return m_pose.position; }
        const Quat&             get_orientation() const { return m_pose.orientation; }
        const Vec3&             get_scale() const { return m_scale; }

        void  set_pose(const ComponentPose& pose) { m_pose = pose; make_dirty(); }
        void  set_position(const Pos3& position) { m_pose.position = position; make_dirty(); }
        void  set_orientation(const Quat& orientation) { m_pose.orientation = orientation; make_dirty(); }
        void  set_scale(const Vec3& scale) { m_scale = scale; make_dirty(); }

    public:
        // 进行 旋转、平移、缩放变换
        void  translate(const Vec3& delta);         // 增量平移
        void  rotate(const Quat& delta);            // 增量旋转（局部坐标系下叠加）
        void  scale(const Vec3& factor);            // 增量缩放

    public:
        Mat4         get_model_matrix();             // 获取模型矩阵 T * R * S，并清除懒标记
        const Mat4&  get_model_matrix() const;
        const Mat4&  get_world_matrix() const;       // 获取世界矩阵（带缓存，合并父级矩阵）

    public:
        // Type id
        Component::TypeID get_type_id() const override
        {
            return get_static_type_id<TransformComponent>();
        }

    public:
        // 序列化与反序列化
        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;
        std::string get_component_type_name() const override { return "TransformComponent"; }

    private:
        ComponentPose m_pose;
        Vec3 m_scale = Vec3(1.0f, 1.0f, 1.0f);

        mutable bool m_dirty = true;                        // 本地矩阵懒加载标记
        void make_dirty() const;                            // 标记脏，并向下传播世界矩阵脏标记
        void clear_dirty() const { m_dirty = false; }
        bool is_dirty() const { return m_dirty; }

        mutable Mat4 m_model = Math::get_identity_mat4();   // 本地矩阵缓存

        mutable bool m_world_dirty = true;                  // 世界矩阵懒加载标记
        mutable Mat4 m_world_cache = Math::get_identity_mat4(); // 世界矩阵缓存
        void propagate_world_dirty() const;                 // 向下传播世界矩阵脏标记
    };
} // namespace ID