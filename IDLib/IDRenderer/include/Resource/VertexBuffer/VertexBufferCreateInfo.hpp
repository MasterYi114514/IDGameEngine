#pragma once

#include "Resource/VertexBuffer/VertexBufferAttribute.hpp"
#include "Resource/VertexBuffer/VertexBufferLayout.hpp"

namespace ID
{
    /*
    *   VertexBufferCreateInfo 结构体用于描述创建顶点缓冲区所需的信息
    *   要求：比如在外部创建一个 VertexBufferLayout 对象，并将其引用传入 VertexBufferCreateInfo 中
    *   注意：VertexBufferCreateInfo 中的 layout 引用必须在 VertexBufferCreateInfo 的生命周期内保持有效，避免悬空引用
    */
    struct IDR_API VertexBufferCreateInfo
    {
        VertexBufferCreateInfo() = delete;
        VertexBufferCreateInfo(const float* data, uint32_t count, VertexBufferLayout& layout)
            : vertex_data(data), vertex_count(count), layout(layout) { }

        const float*            vertex_data = nullptr;            // 顶点数据指针
        uint32_t                vertex_count = 0;                 // 顶点的个数
        VertexBufferLayout&     layout;                             // 顶点布局描述
    };
} // namespace ID