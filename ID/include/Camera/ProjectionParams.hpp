#pragma once

namespace ID
{
    enum class ProjectionType
    {
        Perspective,
        Orthographic
    };

    struct PerspectiveProjection
    {
        float fov_y;
        float aspect;
    };

    struct OrthographicProjection
    {
        float left;
        float right;
        float bottom;
        float top;
    };

    struct ProjectionParams
    {
        ProjectionParams() : type(ProjectionType::Perspective) , persp{ 60.0f, 16.0f / 9.0f }
            , near_z(0.1f) , far_z(1000.0f) { }

        ProjectionType type;

        union
        {
            PerspectiveProjection  persp;
            OrthographicProjection ortho;
        };

        float near_z;
        float far_z;
    };
} // namespace ID