#include "Renderer/IDRCore.hpp"
#include "Renderer/Geometry/Geometry.hpp"

namespace
{
    const ID::VertexBufferLayout default_layout = 
    {
        { "aPos",     ID::AttributeType::Float3,  false,  0 },
        { "aUV",      ID::AttributeType::Float2,  false,  12 },
        { "aNormal",  ID::AttributeType::Float3,  false,  20 }
    };
} // 匿名命名空间

namespace ID
{
    VertexBufferLayout Geometry::s_layout = default_layout;

    void Geometry::set_layout(const VertexBufferLayout& layout)
    {
        s_layout = layout;
    }

    Geometry* Geometry::create_cuboid(float width, float height, float depth)
    {
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
            20,21,22, 22,23,20,   // 下面
        };

        VertexBufferCreateInfo vb_info(vertices, 24, s_layout);
        VertexBufferID vb = VBManager::create(vb_info);

        IndexBufferCreateInfo ib_info(indices, 36);
        IndexBufferID ib = IBManager::create(ib_info);

        return new Geometry(vb, ib);
    }

    Geometry* Geometry::create_cube(float side_length)
    {
        return create_cuboid(side_length, side_length, side_length);
    }

    Geometry* Geometry::create_sphere(float radius, uint32_t longitude_segments, uint32_t latitude_segments)
    {
        static constexpr float PI = 3.14159265358979323846f;
        const float dlon = 2.0f * PI / static_cast<float>(longitude_segments);
        const float dlat = PI / static_cast<float>(latitude_segments);

        std::vector<float> vertices;
        std::vector<uint32_t> indices;

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

                // UV coordinates
                float u = static_cast<float>(lon) / static_cast<float>(longitude_segments);
                float v = 1.0f - static_cast<float>(lat) / static_cast<float>(latitude_segments);

                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
                vertices.push_back(u);
                vertices.push_back(v);
                // 球体法线 = 归一化位置（球心在原点）
                vertices.push_back(x / radius);
                vertices.push_back(y / radius);
                vertices.push_back(z / radius);
            }
        }

        for(uint32_t lat = 0; lat < latitude_segments; ++lat)
        {
            for(uint32_t lon = 0; lon <= longitude_segments; ++lon)
            {
                uint32_t first = (lat * (longitude_segments + 1)) + lon;
                uint32_t second = first + 1;
                uint32_t third = second + longitude_segments;
                uint32_t fourth = third - 1;

                indices.push_back(first);
                indices.push_back(second);
                indices.push_back(third);

                indices.push_back(third);
                indices.push_back(fourth);
                indices.push_back(first);
            }
        }

        VertexBufferCreateInfo vb_info(vertices.data(), static_cast<uint32_t>(vertices.size() / 8), s_layout);

        VertexBufferID vb = VBManager::create(vb_info);
        IndexBufferCreateInfo ib_info(indices.data(), static_cast<uint32_t>(indices.size()));
        IndexBufferID ib = IBManager::create(ib_info);
        return new Geometry(vb, ib);
    }

} // namespace ID