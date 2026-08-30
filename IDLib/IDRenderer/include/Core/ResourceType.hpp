#pragma once

#include <cstdint>
#include <concepts>

namespace ID
{
    enum class ResourceType : uint8_t
    {
        None = 0,
        VertexBuffer,
        IndexBuffer,
        Shader,
        Pipeline,
        Texture,
        FrameBuffer,
        Sampler,
        UniformBuffer
    };

#define IDR_UINT_IMPL(res_type, type) using res_type##UINT = type;

#ifdef IDRENDERER_LARGE_PROJECT
    IDR_UINT_IMPL(VertexBuffer, std::uint32_t);
    IDR_UINT_IMPL(IndexBuffer,  std::uint32_t);
    IDR_UINT_IMPL(Shader,       std::uint16_t);
    IDR_UINT_IMPL(Pipeline,     std::uint16_t);
    IDR_UINT_IMPL(Texture,      std::uint32_t);
    IDR_UINT_IMPL(FrameBuffer,  std::uint8_t);
    IDR_UINT_IMPL(Sampler,      std::uint16_t);
    IDR_UINT_IMPL(UniformBuffer, std::uint32_t);
#else
    IDR_UINT_IMPL(VertexBuffer, std::uint32_t);
    IDR_UINT_IMPL(IndexBuffer,  std::uint32_t);
    IDR_UINT_IMPL(Shader,       std::uint8_t);
    IDR_UINT_IMPL(Pipeline,     std::uint16_t);
    IDR_UINT_IMPL(Texture,      std::uint32_t);
    IDR_UINT_IMPL(Framebuffer,  std::uint8_t);
    IDR_UINT_IMPL(Sampler,      std::uint16_t);
    IDR_UINT_IMPL(UniformBuffer, std::uint32_t);
#endif


    // concept 约束
#define IDR_ConceptImpl(res_type)                                                                   \
    template<typename T, ResourceType ResType>                                                      \
    concept res_type##Res = (std::same_as<T, res_type##UINT> && ResType == ResourceType::res_type); \

    IDR_ConceptImpl(VertexBuffer);
    IDR_ConceptImpl(IndexBuffer);
    IDR_ConceptImpl(Shader);
    IDR_ConceptImpl(Pipeline);
    IDR_ConceptImpl(Texture);
    IDR_ConceptImpl(FrameBuffer);
    IDR_ConceptImpl(Sampler);
    IDR_ConceptImpl(UniformBuffer);
} // namespace ID