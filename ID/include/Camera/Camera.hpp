#pragma once

#include "IDpch.hpp"
#include "Camera/ProjectionParams.hpp"

namespace ID
{
    struct CameraPose
    {
        Pos3 position   = Pos3(0.0f, 0.0f, 0.0f);
        Vec3 front      = Vec3(0.0f, 0.0f, -1.0f);
        Vec3 up         = Vec3(0.0f, 1.0f, 0.0f);

        Vec3 right() const { return Math::cross(front, up); }
    };

    // 视椎体平面，法向量指向视椎体内部，距离为平面到原点的距离
    struct FrustumPlane
    {
        Vec3 normal;
        float distance;
    };

    // 视椎体，包含六个平面：左、右、上、下、近、远
    struct Frustum
    {
        FrustumPlane planes[6];     // 依次为：左、右、上、下、近、远

        FrustumPlane& left()   { return planes[0]; }
        FrustumPlane& right()  { return planes[1]; }
        FrustumPlane& top()    { return planes[2]; }
        FrustumPlane& bottom() { return planes[3]; }
        FrustumPlane& near()   { return planes[4]; }
        FrustumPlane& far()    { return planes[5]; }
    };

    class ID_API Camera
    {
    public:
        Camera() = default;
        explicit Camera(const CameraPose& pose, const ProjectionParams& projection) 
            : m_pose(pose), m_projection(projection) { }

        ~Camera() = default;

    public:
        // pose 有关操作
        const CameraPose&   get_pose() const { return m_pose; }
        void                set_pose(const CameraPose& pose);
        void                set_position(const Pos3& position);
        void                look_at(const Pos3& target, const Vec3& up = Vec3(0.0f, 1.0f, 0.0f));
        void                set_orientation(const Vec3& front, const Vec3& up);

        // projection 有关操作
        const ProjectionParams&     get_projection() const { return m_projection; }
        void                        set_projection(const ProjectionParams& projection);
        ProjectionType              get_projection_type() const { return m_projection.type; }
        void                        set_projection_type(ProjectionType type);
        void                        set_orthographic(float left, float right, float top, 
                                        float bottom, float near_plane, float far_plane);
        void                        set_perspective(float fov_y, float aspect);
        void                        set_viewport_size(float width, float height);

        // 矩阵查询操作
        const Mat4& get_view_matrix() const { return m_view_matrix; }
        const Mat4& get_projection_matrix() const { return m_projection_matrix; }
        const Frustum& get_frustum() const { return m_frustum; }

        // 视锥体裁剪操作
        // TODO

        // 更新操作
        void update();

    private:
        CameraPose          m_pose;
        ProjectionParams    m_projection;

        Mat4                m_view_matrix;
        Mat4                m_projection_matrix;
        Frustum             m_frustum;

        bool                is_view_dirty = true;
        bool                is_projection_dirty = true;

        void update_view_matrix();
        void update_projection_matrix();
        void update_frustum();
    };
} // namespace ID