#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Mesh/Mesh.hpp"

#include <cmath>

#include "BasicPool.hpp"
#include "Log/Log.hpp"

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

        return MeshFactory::create_mesh(mesh_data);
    }

    MeshID MeshFactory::create_cube(float side_length)
    {
        return create_cuboid(side_length, side_length, side_length);
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
        for (uint32_t lat = 0; lat <= latitude_segments; ++lat)
        {
            float theta = lat * dlat;
            float sin_theta = std::sin(theta);
            float cos_theta = std::cos(theta);

            for (uint32_t lon = 0; lon <= longitude_segments; ++lon)
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
                    x / radius, y / radius, z / radius   // 球体法线 = 归一化位置
                });
            }
        }

        // 索引：标准四边形网格，每格两个三角形
        // lon 只到 L-1（最后一列与第 0 列位置重合，通过 first+1 自动环绕到下一行首列）
        for (uint32_t lat = 0; lat < latitude_segments; ++lat)
        {
            for (uint32_t lon = 0; lon < longitude_segments; ++lon)
            {
                uint32_t a = lat * (longitude_segments + 1) + lon;               // top-left
                uint32_t b = a + 1;                                              // top-right
                uint32_t c = (lat + 1) * (longitude_segments + 1) + lon;        // bottom-left
                uint32_t d = c + 1;                                              // bottom-right

                data.indices.insert(data.indices.end(), { a, b, d,  d, c, a });
            }
        }

        return MeshFactory::create_mesh(data);
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
        for (uint32_t z = 0; z < zv; ++z)
        {
            for (uint32_t x = 0; x < xv; ++x)
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
        for (uint32_t z = 0; z < z_segments; ++z)
        {
            for (uint32_t x = 0; x < x_segments; ++x)
            {
                uint32_t a = z * xv + x;
                uint32_t b = a + 1;
                uint32_t c = a + xv;
                uint32_t d = c + 1;

                // 俯视（+Y 朝下看）逆时针：a(b-l) b(b-r) d(t-r) c(t-l)
                data.indices.insert(data.indices.end(), { a, b, d,  d, c, a });
            }
        }

        return MeshFactory::create_mesh(data);
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

        // ---------- 侧壁：2 × (radial+1) 顶点，法线 = 水平径向 ----------
        const uint32_t wall_base = 0;
        for (uint32_t i = 0; i <= radial_segments; ++i)
        {
            float theta = i * dtheta;
            float cx = std::cos(theta);
            float sz = std::sin(theta);
            float u  = static_cast<float>(i) / static_cast<float>(radial_segments);

            // 底部环（y = -hh）
            data.vertices_data.insert(data.vertices_data.end(), {
                cx * radius, -hh, sz * radius,
                u, 1.0f,
                cx, 0.0f, sz
            });
            // 顶部环（y = +hh）
            data.vertices_data.insert(data.vertices_data.end(), {
                cx * radius,  hh, sz * radius,
                u, 0.0f,
                cx, 0.0f, sz
            });
        }

        for (uint32_t i = 0; i < radial_segments; ++i)
        {
            uint32_t b0 = wall_base + i * 2;
            uint32_t b1 = b0 + 1;
            uint32_t t0 = b0 + 2;
            uint32_t t1 = b0 + 3;
            data.indices.insert(data.indices.end(), { b0, t0, b1,  b1, t0, t1 });
        }

        // ---------- 顶盖：中心 + 环，法线 +Y ----------
        const uint32_t top_center = static_cast<uint32_t>(data.vertices_data.size() / 8);
        data.vertices_data.insert(data.vertices_data.end(), { 0.0f, hh, 0.0f,  0.5f, 0.5f,  0.0f, 1.0f, 0.0f });

        const uint32_t top_ring = static_cast<uint32_t>(data.vertices_data.size() / 8);
        for (uint32_t i = 0; i <= radial_segments; ++i)
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
        for (uint32_t i = 0; i < radial_segments; ++i)
        {
            data.indices.insert(data.indices.end(), {
                top_center, top_ring + i, top_ring + i + 1
            });
        }

        // ---------- 底盖：中心 + 环，法线 -Y ----------
        const uint32_t bottom_center = static_cast<uint32_t>(data.vertices_data.size() / 8);
        data.vertices_data.insert(data.vertices_data.end(), { 0.0f, -hh, 0.0f,  0.5f, 0.5f,  0.0f, -1.0f, 0.0f });

        const uint32_t bottom_ring = static_cast<uint32_t>(data.vertices_data.size() / 8);
        for (uint32_t i = 0; i <= radial_segments; ++i)
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
        for (uint32_t i = 0; i < radial_segments; ++i)
        {
            // 从下方看逆时针：center → i+1 → i
            data.indices.insert(data.indices.end(), {
                bottom_center, bottom_ring + i + 1, bottom_ring + i
            });
        }

        return MeshFactory::create_mesh(data);
    }
}