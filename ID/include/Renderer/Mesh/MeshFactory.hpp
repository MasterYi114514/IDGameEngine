#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/MeshData.hpp"

namespace ID
{
    class MeshFactory;
    class Mesh;

    using MeshID = BasicID<uint32_t, MeshFactory>;

    class ID_API MeshFactory
    {
    public:
        MeshFactory() = delete;
        ~MeshFactory() = delete;

        /**
         *  统一顶点布局： aPos(Float3) + aUV(Float2) + aNormal(Float3)，stride = 32 字节
         */
        static const VertexBufferLayout& get_layout();

        static MeshID   create_mesh(MeshData& mesh_data);
        static Mesh&    get_mesh(MeshID mesh_id);
        static void     destroy_mesh(MeshID mesh_id);

        /**
         *  @brief 创建一个长方体 Mesh
         *  @param width    长方体的长
         *  @param height   长方体的宽
         *  @param depth    长方体的高
         */
        static MeshID create_cuboid(float width, float height, float depth);

        /**
         *  @brief 创建一个正方体 Mesh
         *  @param side_length 正方体的边长
         */
        static MeshID create_cube(float side_length);

        /**
         *  @brief 创建一个球体 Mesh
         *  @param radius 球体的半径
         *  @param longitude_segments 经度分段数（水平分段数）
         *  @param latitude_segments 纬度分段数（垂直分段数）
         */
        static MeshID create_sphere(float radius, uint32_t longitude_segments = 32,
            uint32_t latitude_segments = 16);

        /**
         *  @brief 创建一个平面 Mesh
         *  @param width 平面的宽度
         *  @param depth 平面的深度
         *  @param x_segments 平面在 X 方向的分段数
         *  @param z_segments 平面在 Z 方向的分段数
         */
        static MeshID create_plane(float width, float depth,
            uint32_t x_segments = 1, uint32_t z_segments = 1);

        /**
         *  @brief 创建一个圆柱体 Mesh
         *  @param radius 圆柱体的半径
         *  @param height 圆柱体的高度
         *  @param radial_segments 圆柱体在径向的分段数
         */
        static MeshID create_cylinder(float radius, float height,
            uint32_t radial_segments = 32);
                                
    };
} // namespace ID
