#pragma once

#include "Resource/VertexBuffer/VertexBufferLayout.hpp"
#include "Resource/Pipeline/PipelineState.hpp"
#include "Resource/ResourceID.hpp"

namespace ID
{
    struct IDR_API PipelineCreateInfo
    {
        PipelineCreateInfo() = delete;
        PipelineCreateInfo(const ShaderID shaderID, const VertexBufferLayout& layout, 
            const PipelineState& pipelineState = PipelineState())
            : shaderID(shaderID), layout(layout), pipelineState(pipelineState) { }

        const ShaderID          shaderID;                   // 着色器ID
        VertexBufferLayout      layout;                     // 顶点布局
        PipelineState           pipelineState;              // 管线状态
    };
} // namespace ID