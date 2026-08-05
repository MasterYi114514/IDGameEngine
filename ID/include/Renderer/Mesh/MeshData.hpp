#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    struct MeshData
    {
        std::vector<float>      vertices_data;      // 顶点数据
        std::vector<uint32_t>   indices;
        VertexBufferLayout      layout;

        uint32_t get_vertex_count() const
        {
            uint32_t floats_per_vertex = layout.get_stride() / sizeof(float);
            if (floats_per_vertex == 0)
            {
                return 0;
            }
            return static_cast<uint32_t>(vertices_data.size() / floats_per_vertex);
        }

        bool is_valid() const
        {
            return !vertices_data.empty() && !indices.empty() && layout.get_stride() > 0;
        }
    };
} // namespace ID