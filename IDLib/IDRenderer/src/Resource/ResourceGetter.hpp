#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/VertexBuffer/VertexBuffer.hpp"
#include "Resource/IndexBuffer/IndexBuffer.hpp"
#include "Resource/Shader/Shader.hpp"
#include "Resource/Texture/Texture.hpp"
#include "Resource/Pipeline/Pipeline.hpp"
#include "Resource/FrameBuffer/FrameBuffer.hpp"
#include "Resource/UniformBuffer/UniformBuffer.hpp"

namespace ID::ResourceGetter
{
    VertexBuffer*   get_vertex_buffer   (const VertexBufferID   id);
    IndexBuffer*    get_index_buffer    (const IndexBufferID    id);
    Shader*         get_shader          (const ShaderID         id);
    Texture*        get_texture         (const TextureID        id);
    Pipeline*       get_pipeline        (const PipelineID       id);
    FrameBuffer*    get_frame_buffer    (const FrameBufferID    id);
    UniformBuffer*  get_uniform_buffer  (const UniformBufferID  id);

} // namespace ID::ResourceGetter

// 宏定义，方便获取对应的 Resource 对象
#define IDR_ResVB(id)           ::ID::ResourceGetter::get_vertex_buffer(id)
#define IDR_ResIB(id)           ::ID::ResourceGetter::get_index_buffer(id)
#define IDR_ResShader(id)       ::ID::ResourceGetter::get_shader(id)
#define IDR_ResTexture(id)      ::ID::ResourceGetter::get_texture(id)
#define IDR_ResPipeline(id)     ::ID::ResourceGetter::get_pipeline(id)
#define IDR_ResFB(id)           ::ID::ResourceGetter::get_frame_buffer(id)
#define IDR_ResUBO(id)          ::ID::ResourceGetter::get_uniform_buffer(id)