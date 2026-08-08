#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/MeshData.hpp"

#include <string>
#include <vector>

namespace ID
{
    class MeshFactory;
    class Mesh;

    using MeshID = BasicID<uint32_t, MeshFactory>;

    /*
    *  MeshSourceType 枚举表示 Mesh 的来源类型：None（未知）、Primitive（程序化图元）、File（从文件加载）。
    */
    enum class MeshSourceType : uint8_t
    {
        None = 0,
        Primitive,
        File
    };

    /*
    *  MeshPrimitiveType 枚举表示 ID 当前提供的内置图元类型
    */
    enum class MeshPrimitiveType : uint8_t
    {
        None = 0,
        Cube,
        Cuboid,
        Sphere,
        Plane,
        Cylinder
    };

    /*
    *  MeshPrimitiveParams 联合体存储各图元的创建参数，与 MeshFactory::create_* 签名一一对应。
    */
    union MeshPrimitiveParams
    {
        struct { float side_length; }                                    cube;
        struct { float width, height, depth; }                           cuboid;
        struct { float radius; uint32_t longitude_segments, latitude_segments; } sphere;
        struct { float width, depth; uint32_t x_segments, z_segments; }  plane;
        struct { float radius, height; uint32_t radial_segments; }       cylinder;
    };

    /*
    *  MeshSourceDesc 结构体描述一个 MeshID 的来源信息，供序列化/反序列化使用。
    *  - source_type == File 时，file_path 和 submesh_index 有效
    *  - source_type == Primitive 时，primitive_type 和 primitive_params 有效
    */
    struct MeshSourceDesc
    {
        MeshSourceType      source_type = MeshSourceType::None;

        // File 来源
        std::string         file_path;
        uint32_t            submesh_index = 0;

        // Primitive 来源
        MeshPrimitiveType   primitive_type = MeshPrimitiveType::None;
        MeshPrimitiveParams primitive_params;
    };

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

        // ── 图元创建 ──

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

        // ── 文件加载（内部调用 IDAsset::MeshLoader）──

        /**
         *  @brief 从文件加载单个子 Mesh（通过 Assimp）
         *  @param path           模型文件路径（.obj / .fbx / .gltf 等）
         *  @param submesh_index  子 Mesh 索引，默认 0
         *  @return 有效的 MeshID，失败返回 invalid_id
         */
        static MeshID create_mesh_from_file(const std::string& path, uint32_t submesh_index = 0);

        /**
         *  @brief 从文件加载所有子 Mesh
         *  @param path 模型文件路径
         *  @return MeshID 列表，文件中的每个子 Mesh 对应一个 ID
         */
        static std::vector<MeshID> create_meshes_from_file(const std::string& path);

        // ── 来源追踪 ──

        /**
         *  @brief 获取指定 MeshID 的来源描述
         *  @return 来源描述指针，不存在则返回 nullptr
         */
        static const MeshSourceDesc* get_source_desc(MeshID mesh_id);

        /**
         *  @brief 序列化指定的 Mesh
         *  @param mesh_id MeshID
         *  @param arena ArenaID
         *  @return 序列化后的 Json 对象
         */
        static Json serialize(MeshID mesh_id, ArenaID arena);

        static MeshID deserialize(const Json& json);

    private:
        static void register_source(MeshID mesh_id, MeshSourceDesc desc);
    };
} // namespace ID
