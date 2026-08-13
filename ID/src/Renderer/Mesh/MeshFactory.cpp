#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Mesh/Mesh.hpp"

#include <cmath>

#include "BasicPool.hpp"
#include "Log/Log.hpp"
#include "Loader/MeshLoader.hpp"

namespace
{
    const ID::VertexBufferLayout default_layout =
    {
        { "aPos",     ID::AttributeType::Float3,  false,  0 },
        { "aUV",      ID::AttributeType::Float2,  false,  12 },
        { "aNormal",  ID::AttributeType::Float3,  false,  20 }
    };

    constexpr float PI = 3.14159265358979323846f;

    using MeshPool = ID::BasicPool<ID::MeshID::UnderlyingType, ID::Mesh>;
    MeshPool    g_mesh_pool;

    // 来源追踪：与 g_mesh_pool.m_pool 同步增长
    std::vector<ID::MeshSourceDesc> g_mesh_source_descs;
} // 匿名命名空间

namespace ID
{
    const VertexBufferLayout& MeshFactory::get_layout()
    {
        return default_layout;
    }

    MeshID MeshFactory::create_mesh(MeshData& mesh_data)
    {
        if(!mesh_data.is_valid())
        {
            return MeshID::invalid_id();
        }

        MeshID::UnderlyingType id = g_mesh_pool.search_slot();
        if(MeshID::invalid_id() == id)
        {
            ID_WARN("MeshFactory MeshPool 已满，请选择更大的类型作为 MeshID的底层类型");
            return MeshID::invalid_id();
        }

        if(id >= g_mesh_pool.m_pool.size())
        {
            g_mesh_pool.m_pool.emplace_back(Mesh(mesh_data));
        }
        else
        {
            g_mesh_pool.m_pool[id] = Mesh(mesh_data);
        }

        return MeshID{id};
    }

    Mesh& MeshFactory::get_mesh(MeshID mesh_id)
    {
        if(!mesh_id.is_valid())
        {
            ID_ERROR("尝试获取无效的 MeshID");
            throw std::runtime_error("尝试获取不存在的 MeshID");
        }

        if(mesh_id.get_id() >= g_mesh_pool.m_pool.size())
        {
            ID_ERROR("尝试获取不存在的 MeshID: {}", mesh_id.get_id());
            throw std::runtime_error("尝试获取不存在的 MeshID");
        }

        return g_mesh_pool.m_pool[mesh_id.get_id()];
    }

    void MeshFactory::destroy_mesh(MeshID mesh_id)
    {
        if(!mesh_id.is_valid())
        {
            ID_ERROR("尝试销毁无效的 MeshID");
            return;
        }

        if(mesh_id.get_id() >= g_mesh_pool.m_pool.size())
        {
            ID_ERROR("尝试销毁不存在的 MeshID: {}", mesh_id.get_id());
            return;
        }

        g_mesh_pool.destroy(mesh_id.get_id());

        // 清理来源追踪
        if(mesh_id.get_id() < g_mesh_source_descs.size())
        {
            g_mesh_source_descs[mesh_id.get_id()] = MeshSourceDesc();
        }
    }

    void MeshFactory::register_source(MeshID mesh_id, MeshSourceDesc desc)
    {
        if(!mesh_id.is_valid()) return;

        uint32_t idx = mesh_id.get_id();
        if(idx >= g_mesh_source_descs.size())
        {
            g_mesh_source_descs.resize(idx + 1);
        }
        g_mesh_source_descs[idx] = std::move(desc);
    }

    const MeshSourceDesc* MeshFactory::get_source_desc(MeshID mesh_id)
    {
        if(!mesh_id.is_valid()) return nullptr;

        uint32_t idx = mesh_id.get_id();
        if(idx >= g_mesh_source_descs.size()) return nullptr;
        if(g_mesh_source_descs[idx].source_type == MeshSourceType::None) return nullptr;

        return &g_mesh_source_descs[idx];
    }

    MeshID MeshFactory::create_cuboid(float width, float height, float depth)
    {
        // 准备数据
        float hw = width  * 0.5f;
        float hh = height * 0.5f;
        float hd = depth  * 0.5f;

        // 24 个顶点（6 个面 × 4 顶点/面），每个顶点 8 个 float：位置(3) + UV(2) + 法线(3)
        // 法线方向：后面(0,0,-1)  前面(0,0,1)  右面(1,0,0)  左面(-1,0,0)  上面(0,1,0)  下面(0,-1,0)
        float vertices[192] = {
            // 后面 (z = -hd), 法线 (0, 0, -1)
            -hw, -hh, -hd,  0.0f, 0.0f,   0.0f,  0.0f, -1.0f,   // 0: 后-左下
             hw, -hh, -hd,  1.0f, 0.0f,   0.0f,  0.0f, -1.0f,   // 1: 后-右下
             hw,  hh, -hd,  1.0f, 1.0f,   0.0f,  0.0f, -1.0f,   // 2: 后-右上
            -hw,  hh, -hd,  0.0f, 1.0f,   0.0f,  0.0f, -1.0f,   // 3: 后-左上

            // 前面 (z = +hd), 法线 (0, 0, 1)
            -hw, -hh,  hd,  0.0f, 0.0f,   0.0f,  0.0f,  1.0f,   // 4: 前-左下
             hw, -hh,  hd,  1.0f, 0.0f,   0.0f,  0.0f,  1.0f,   // 5: 前-右下
             hw,  hh,  hd,  1.0f, 1.0f,   0.0f,  0.0f,  1.0f,   // 6: 前-右上
            -hw,  hh,  hd,  0.0f, 1.0f,   0.0f,  0.0f,  1.0f,   // 7: 前-左上

            // 右面 (x = +hw), 法线 (1, 0, 0)
             hw, -hh, -hd,  0.0f, 0.0f,   1.0f,  0.0f,  0.0f,   // 8:  右-后下
             hw, -hh,  hd,  1.0f, 0.0f,   1.0f,  0.0f,  0.0f,   // 9:  右-前下
             hw,  hh,  hd,  1.0f, 1.0f,   1.0f,  0.0f,  0.0f,   // 10: 右-前上
             hw,  hh, -hd,  0.0f, 1.0f,   1.0f,  0.0f,  0.0f,   // 11: 右-后上

            // 左面 (x = -hw), 法线 (-1, 0, 0)
            -hw, -hh,  hd,  0.0f, 0.0f,  -1.0f,  0.0f,  0.0f,   // 12: 左-前下
            -hw, -hh, -hd,  1.0f, 0.0f,  -1.0f,  0.0f,  0.0f,   // 13: 左-后下
            -hw,  hh, -hd,  1.0f, 1.0f,  -1.0f,  0.0f,  0.0f,   // 14: 左-后上
            -hw,  hh,  hd,  0.0f, 1.0f,  -1.0f,  0.0f,  0.0f,   // 15: 左-前上

            // 上面 (y = +hh), 法线 (0, 1, 0)
            -hw,  hh, -hd,  0.0f, 0.0f,   0.0f,  1.0f,  0.0f,   // 16: 上-后左
             hw,  hh, -hd,  1.0f, 0.0f,   0.0f,  1.0f,  0.0f,   // 17: 上-后右
             hw,  hh,  hd,  1.0f, 1.0f,   0.0f,  1.0f,  0.0f,   // 18: 上-前右
            -hw,  hh,  hd,  0.0f, 1.0f,   0.0f,  1.0f,  0.0f,   // 19: 上-前左

            // 下面 (y = -hh), 法线 (0, -1, 0)
            -hw, -hh,  hd,  0.0f, 0.0f,   0.0f, -1.0f,  0.0f,   // 20: 下-前左
             hw, -hh,  hd,  1.0f, 0.0f,   0.0f, -1.0f,  0.0f,   // 21: 下-前右
             hw, -hh, -hd,  1.0f, 1.0f,   0.0f, -1.0f,  0.0f,   // 22: 下-后右
            -hw, -hh, -hd,  0.0f, 1.0f,   0.0f, -1.0f,  0.0f,   // 23: 下-后左
        };

        // 每面两个三角形，保持逆时针 winding
        uint32_t indices[36] = {
             0, 2, 1,  2, 0, 3,   // 后面
             4, 5, 6,  6, 7, 4,   // 前面
             8,10, 9, 10, 8,11,   // 右面
            12,14,13, 14,12,15,   // 左面
            16,18,17, 18,16,19,   // 上面
            20,23,22, 22,21,20,   // 下面
        };

        MeshData mesh_data;
        mesh_data.layout = default_layout;
        mesh_data.vertices_data.assign(vertices, vertices + 192);
        mesh_data.indices.assign(indices, indices + 36);

        MeshID id = MeshFactory::create_mesh(mesh_data);

        MeshSourceDesc desc;
        desc.source_type = MeshSourceType::Primitive;
        desc.primitive_type = MeshPrimitiveType::Cuboid;
        desc.primitive_params.cuboid.width = width;
        desc.primitive_params.cuboid.height = height;
        desc.primitive_params.cuboid.depth = depth;
        register_source(id, std::move(desc));

        return id;
    }

    MeshID MeshFactory::create_cube(float side_length)
    {
        MeshID id = create_cuboid(side_length, side_length, side_length);

        MeshSourceDesc desc;
        desc.source_type = MeshSourceType::Primitive;
        desc.primitive_type = MeshPrimitiveType::Cube;
        desc.primitive_params.cube.side_length = side_length;
        register_source(id, std::move(desc));

        return id;
    }

    // =====================================================================
    //  球体（经纬网格，法线 = 归一化位置）
    // =====================================================================
    MeshID MeshFactory::create_sphere(float radius, uint32_t longitude_segments, uint32_t latitude_segments)
    {
        MeshData data;
        data.layout = default_layout;

        const float dlon = 2.0f * PI / static_cast<float>(longitude_segments);
        const float dlat = PI / static_cast<float>(latitude_segments);

        // 生成顶点：纬度从北极到南极，经度绕 Y 轴
        for(uint32_t lat = 0; lat <= latitude_segments; ++lat)
        {
            float theta = lat * dlat;
            float sin_theta = std::sin(theta);
            float cos_theta = std::cos(theta);

            for(uint32_t lon = 0; lon <= longitude_segments; ++lon)
            {
                float phi = lon * dlon;
                float sin_phi = std::sin(phi);
                float cos_phi = std::cos(phi);

                float x = radius * sin_theta * cos_phi;
                float y = radius * cos_theta;
                float z = radius * sin_theta * sin_phi;

                float u = static_cast<float>(lon) / static_cast<float>(longitude_segments);
                float v = 1.0f - static_cast<float>(lat) / static_cast<float>(latitude_segments);

                data.vertices_data.insert(data.vertices_data.end(), {
                    x, y, z,
                    u, v,
                    x / radius, y / radius, z / radius
                });
            }
        }

        for(uint32_t lat = 0; lat < latitude_segments; ++lat)
        {
            for(uint32_t lon = 0; lon < longitude_segments; ++lon)
            {
                uint32_t a = lat * (longitude_segments + 1) + lon;
                uint32_t b = a + 1;
                uint32_t c = (lat + 1) * (longitude_segments + 1) + lon;
                uint32_t d = c + 1;

                data.indices.insert(data.indices.end(), { a, b, d,  d, c, a });
            }
        }

        MeshID id = MeshFactory::create_mesh(data);

        MeshSourceDesc desc;
        desc.source_type = MeshSourceType::Primitive;
        desc.primitive_type = MeshPrimitiveType::Sphere;
        desc.primitive_params.sphere.radius = radius;
        desc.primitive_params.sphere.longitude_segments = longitude_segments;
        desc.primitive_params.sphere.latitude_segments = latitude_segments;
        register_source(id, std::move(desc));

        return id;
    }

    // =====================================================================
    //  平面（XZ 平面，y = 0，法线 +Y 朝上）
    // =====================================================================
    MeshID MeshFactory::create_plane(float width, float depth, uint32_t x_segments, uint32_t z_segments)
    {
        MeshData data;
        data.layout = default_layout;

        const float hw = width  * 0.5f;
        const float hd = depth  * 0.5f;
        const uint32_t xv = x_segments + 1;
        const uint32_t zv = z_segments + 1;

        data.vertices_data.reserve(xv * zv * 8);
        for(uint32_t z = 0; z < zv; ++z)
        {
            for(uint32_t x = 0; x < xv; ++x)
            {
                float u = static_cast<float>(x) / static_cast<float>(x_segments);
                float v = static_cast<float>(z) / static_cast<float>(z_segments);
                float px = -hw + u * width;
                float pz = -hd + v * depth;

                data.vertices_data.insert(data.vertices_data.end(), {
                    px, 0.0f, pz,
                    u, v,
                    0.0f, 1.0f, 0.0f
                });
            }
        }

        data.indices.reserve(x_segments * z_segments * 6);
        for(uint32_t z = 0; z < z_segments; ++z)
        {
            for(uint32_t x = 0; x < x_segments; ++x)
            {
                uint32_t a = z * xv + x;
                uint32_t b = a + 1;
                uint32_t c = a + xv;
                uint32_t d = c + 1;

                data.indices.insert(data.indices.end(), { a, b, d,  d, c, a });
            }
        }

        MeshID id = MeshFactory::create_mesh(data);

        MeshSourceDesc desc;
        desc.source_type = MeshSourceType::Primitive;
        desc.primitive_type = MeshPrimitiveType::Plane;
        desc.primitive_params.plane.width = width;
        desc.primitive_params.plane.depth = depth;
        desc.primitive_params.plane.x_segments = x_segments;
        desc.primitive_params.plane.z_segments = z_segments;
        register_source(id, std::move(desc));

        return id;
    }

    // =====================================================================
    //  圆柱体（侧壁 + 顶盖 + 底盖，每面独立顶点）
    // =====================================================================
    MeshID MeshFactory::create_cylinder(float radius, float height, uint32_t radial_segments)
    {
        MeshData data;
        data.layout = default_layout;

        const float hh = height * 0.5f;
        const float dtheta = 2.0f * PI / static_cast<float>(radial_segments);

        const uint32_t wall_base = 0;
        for(uint32_t i = 0; i <= radial_segments; ++i)
        {
            float theta = i * dtheta;
            float cx = std::cos(theta);
            float sz = std::sin(theta);
            float u  = static_cast<float>(i) / static_cast<float>(radial_segments);

            data.vertices_data.insert(data.vertices_data.end(), {
                cx * radius, -hh, sz * radius,
                u, 1.0f,
                cx, 0.0f, sz
            });
            data.vertices_data.insert(data.vertices_data.end(), {
                cx * radius,  hh, sz * radius,
                u, 0.0f,
                cx, 0.0f, sz
            });
        }

        for(uint32_t i = 0; i < radial_segments; ++i)
        {
            uint32_t b0 = wall_base + i * 2;
            uint32_t b1 = b0 + 1;
            uint32_t t0 = b0 + 2;
            uint32_t t1 = b0 + 3;
            data.indices.insert(data.indices.end(), { b0, t0, b1,  b1, t0, t1 });
        }

        const uint32_t top_center = static_cast<uint32_t>(data.vertices_data.size() / 8);
        data.vertices_data.insert(data.vertices_data.end(), { 0.0f, hh, 0.0f,  0.5f, 0.5f,  0.0f, 1.0f, 0.0f });

        const uint32_t top_ring = static_cast<uint32_t>(data.vertices_data.size() / 8);
        for(uint32_t i = 0; i <= radial_segments; ++i)
        {
            float theta = i * dtheta;
            float u = 0.5f + 0.5f * std::cos(theta);
            float v = 0.5f + 0.5f * std::sin(theta);
            data.vertices_data.insert(data.vertices_data.end(), {
                std::cos(theta) * radius, hh, std::sin(theta) * radius,
                u, v,
                0.0f, 1.0f, 0.0f
            });
        }
        for(uint32_t i = 0; i < radial_segments; ++i)
        {
            data.indices.insert(data.indices.end(), {
                top_center, top_ring + i, top_ring + i + 1
            });
        }

        const uint32_t bottom_center = static_cast<uint32_t>(data.vertices_data.size() / 8);
        data.vertices_data.insert(data.vertices_data.end(), { 0.0f, -hh, 0.0f,  0.5f, 0.5f,  0.0f, -1.0f, 0.0f });

        const uint32_t bottom_ring = static_cast<uint32_t>(data.vertices_data.size() / 8);
        for(uint32_t i = 0; i <= radial_segments; ++i)
        {
            float theta = i * dtheta;
            float u = 0.5f + 0.5f * std::cos(theta);
            float v = 0.5f + 0.5f * std::sin(theta);
            data.vertices_data.insert(data.vertices_data.end(), {
                std::cos(theta) * radius, -hh, std::sin(theta) * radius,
                u, v,
                0.0f, -1.0f, 0.0f
            });
        }
        for(uint32_t i = 0; i < radial_segments; ++i)
        {
            data.indices.insert(data.indices.end(), {
                bottom_center, bottom_ring + i + 1, bottom_ring + i
            });
        }

        MeshID id = MeshFactory::create_mesh(data);

        MeshSourceDesc desc;
        desc.source_type = MeshSourceType::Primitive;
        desc.primitive_type = MeshPrimitiveType::Cylinder;
        desc.primitive_params.cylinder.radius = radius;
        desc.primitive_params.cylinder.height = height;
        desc.primitive_params.cylinder.radial_segments = radial_segments;
        register_source(id, std::move(desc));

        return id;
    }

    // =====================================================================
    //  从文件加载（通过 IDAsset::MeshLoader / Assimp）
    // =====================================================================

    MeshID MeshFactory::create_mesh_from_file(const std::string& path, uint32_t submesh_index)
    {
        MeshLoadResult result = MeshLoader::load_meshes(path);
        if(!result.success || result.meshes.empty())
        {
            ID_ERROR("MeshFactory::create_mesh_from_file：加载失败: {}", path);
            return MeshID::invalid_id();
        }

        if(submesh_index >= result.meshes.size())
        {
            ID_ERROR("MeshFactory::create_mesh_from_file：子 Mesh 索引 {} 超出范围（共 {} 个）",
                submesh_index, result.meshes.size());
            return MeshID::invalid_id();
        }

        RawMeshData& raw = result.meshes[submesh_index];

        MeshData engine_data;
        engine_data.layout = default_layout;
        engine_data.vertices_data = std::move(raw.vertices_data);
        engine_data.indices = std::move(raw.indices);

        MeshID id = create_mesh(engine_data);

        MeshSourceDesc desc;
        desc.source_type = MeshSourceType::File;
        desc.file_path = path;
        desc.submesh_index = submesh_index;
        register_source(id, std::move(desc));

        return id;
    }

    std::vector<MeshID> MeshFactory::create_meshes_from_file(const std::string& path)
    {
        std::vector<MeshID> result_ids;

        MeshLoadResult result = MeshLoader::load_meshes(path);
        if(!result.success || result.meshes.empty())
        {
            ID_ERROR("MeshFactory::create_meshes_from_file：加载失败: {}", path);
            return result_ids;
        }

        result_ids.reserve(result.meshes.size());
        for(uint32_t i = 0; i < static_cast<uint32_t>(result.meshes.size()); ++i)
        {
            RawMeshData& raw = result.meshes[i];

            MeshData engine_data;
            engine_data.layout = default_layout;
            engine_data.vertices_data = std::move(raw.vertices_data);
            engine_data.indices = std::move(raw.indices);

            MeshID id = create_mesh(engine_data);

            MeshSourceDesc desc;
            desc.source_type = MeshSourceType::File;
            desc.file_path = path;
            desc.submesh_index = i;
            register_source(id, std::move(desc));

            result_ids.push_back(id);
        }

        return result_ids;
    }

    Json MeshFactory::serialize(MeshID mesh_id, ArenaID arena)
    {
        if(!mesh_id.is_valid())
        {
            ID_ERROR("尝试使用无效的 MeshID 进行序列化");
            return JSON::null;
        }

        const MeshSourceDesc* desc = get_source_desc(mesh_id);
        if(!desc)
        {
            ID_ERROR("尝试序列化不存在来源追踪的 MeshID: {}", mesh_id.get_id());
            return JSON::null;
        }

        Json result = Json::create_object(arena);
        switch(desc->source_type)
        {
            case MeshSourceType::File:
            {
                result["source_type"] = Json::create_string("File", arena);
                Json info = Json::create_object(arena);
                info["file_path"] = Json::create_string(desc->file_path, arena);
                info["submesh_index"] = Json(static_cast<int>(desc->submesh_index));
                result["info"] = info;
                break;
            }
            case MeshSourceType::Primitive:
            {
                result["source_type"] = Json::create_string("Primitive", arena);
                Json info = Json::create_object(arena);
                auto& params = desc->primitive_params;
                switch(desc->primitive_type)
                {
                    case MeshPrimitiveType::Cuboid:
                        info["primitive_type"] = Json::create_string("Cuboid", arena);
                        info["width"] = Json(static_cast<double>(params.cuboid.width));
                        info["height"] = Json(static_cast<double>(params.cuboid.height));
                        info["depth"] = Json(static_cast<double>(params.cuboid.depth));
                        break;
                    case MeshPrimitiveType::Cube:
                        info["primitive_type"] = Json::create_string("Cube", arena);
                        info["side_length"] = Json(static_cast<double>(params.cube.side_length));
                        break;
                    case MeshPrimitiveType::Sphere:
                        info["primitive_type"] = Json::create_string("Sphere", arena);
                        info["radius"] = Json(static_cast<double>(params.sphere.radius));
                        info["longitude_segments"] = Json(static_cast<int>(params.sphere.longitude_segments));
                        info["latitude_segments"] = Json(static_cast<int>(params.sphere.latitude_segments));
                        break;
                    case MeshPrimitiveType::Plane:
                        info["primitive_type"] = Json::create_string("Plane", arena);
                        info["width"] = Json(static_cast<double>(params.plane.width));
                        info["depth"] = Json(static_cast<double>(params.plane.depth));
                        info["x_segments"] = Json(static_cast<int>(params.plane.x_segments));
                        info["z_segments"] = Json(static_cast<int>(params.plane.z_segments));
                        break;
                    case MeshPrimitiveType::Cylinder:
                        info["primitive_type"] = Json::create_string("Cylinder", arena);
                        info["radius"] = Json(static_cast<double>(params.cylinder.radius));
                        info["height"] = Json(static_cast<double>(params.cylinder.height));
                        info["radial_segments"] = Json(static_cast<int>(params.cylinder.radial_segments));
                        break;
                    default:
                        ID_ERROR("未知的 Primitive 类型，无法序列化");
                        return JSON::null;
                }
                result["info"] = info;
                break;
            }
        }

        return result;
    }

    MeshID MeshFactory::deserialize(const Json& json)
    {
        if (!json.is_object())
        {
            ID_ERROR("MeshFactory::deserialize: json 不是对象类型，无法反序列化 Mesh");
            return MeshID::invalid_id();
        }

        std::string source_type = json["source_type"].as_cstr();

        if (source_type == "File")
        {
            const Json& info = json["info"];
            if (!info.is_object())
            {
                ID_ERROR("MeshFactory::deserialize: File 类型的 Mesh 缺少有效的 info 字段");
                return MeshID::invalid_id();
            }

            std::string path = info["file_path"].as_cstr();
            uint32_t submesh = static_cast<uint32_t>(info["submesh_index"].as_int());
            return create_mesh_from_file(path, submesh);
        }
        else if (source_type == "Primitive")
        {
            const Json& info = json["info"];
            if (!info.is_object())
            {
                ID_ERROR("MeshFactory::deserialize: Primitive 类型的 Mesh 缺少有效的 info 字段");
                return MeshID::invalid_id();
            }

            std::string prim_type = info["primitive_type"].as_cstr();

            if (prim_type == "Cube")
            {
                float sl = static_cast<float>(info["side_length"].as_float());
                return create_cube(sl);
            }
            else if (prim_type == "Cuboid")
            {
                float w = static_cast<float>(info["width"].as_float());
                float h = static_cast<float>(info["height"].as_float());
                float d = static_cast<float>(info["depth"].as_float());
                return create_cuboid(w, h, d);
            }
            else if (prim_type == "Sphere")
            {
                float r = static_cast<float>(info["radius"].as_float());
                uint32_t lon = static_cast<uint32_t>(info["longitude_segments"].as_int());
                uint32_t lat = static_cast<uint32_t>(info["latitude_segments"].as_int());
                return create_sphere(r, lon, lat);
            }
            else if (prim_type == "Plane")
            {
                float w = static_cast<float>(info["width"].as_float());
                float d = static_cast<float>(info["depth"].as_float());
                uint32_t xs = static_cast<uint32_t>(info["x_segments"].as_int());
                uint32_t zs = static_cast<uint32_t>(info["z_segments"].as_int());
                return create_plane(w, d, xs, zs);
            }
            else if (prim_type == "Cylinder")
            {
                float r = static_cast<float>(info["radius"].as_float());
                float h = static_cast<float>(info["height"].as_float());
                uint32_t rs = static_cast<uint32_t>(info["radial_segments"].as_int());
                return create_cylinder(r, h, rs);
            }
            else
            {
                ID_ERROR("MeshFactory::deserialize: 未知的 Primitive 类型 '{}'", prim_type);
                return MeshID::invalid_id();
            }
        }

        // source_type 为 "None" 或其他未知值
        return MeshID::invalid_id();
    }
} // namespace ID
