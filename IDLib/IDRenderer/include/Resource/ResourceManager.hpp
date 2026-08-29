#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/ResourceID.hpp"
#include "Resource/VertexBuffer/VertexBufferCreateInfo.hpp"
#include "Resource/IndexBuffer/IndexBufferCreateInfo.hpp"
#include "Resource/Shader/ShaderCreateInfo.hpp"
#include "Resource/Shader/ShaderUniformDesc.hpp"
#include "Resource/Texture/TextureCreateInfo.hpp"
#include "Resource/Pipeline/PipelineCreateInfo.hpp"
#include "Resource/FrameBuffer/FrameBufferCreateInfo.hpp"
#include "Resource/UniformBuffer/UniformBufferCreateInfo.hpp"

namespace ID
{
    /*
    *   ResourceManager 是一个静态模版类，不会进行实例化
    *   该类用于管理各种资源的创建、销毁和访问
    *   资源类型由模版参数 ResType 指定，资源的唯一标识符由模版参数 T 指定
    *   该类为每种资源都提供 create、destory 方法
    *   create 方法用于创建资源，返回一个 ResourceID 对象，该对象包含资源的唯一标识符
    */
    template<std::unsigned_integral T, ResourceType ResType>
    class IDR_API ResourceManager
    {
    public:
        ResourceManager() = delete;
        ~ResourceManager() = delete;

    public:
        // VertexBuffer 资源接口
        static VertexBufferID   create(const VertexBufferCreateInfo& create_info) 
            requires VertexBufferRes<T, ResType>;
        static uint32_t         get_vertex_count(const VertexBufferID& id)
            requires VertexBufferRes<T, ResType>;
        static void             destroy(const VertexBufferID& id)
            requires VertexBufferRes<T, ResType>;

        // IndexBuffer 资源接口
        static IndexBufferID    create(const IndexBufferCreateInfo&     create_info)
            requires IndexBufferRes<T, ResType>;

        // Shader 资源接口
        static ShaderID         create(const ShaderCreateInfo&          create_info)
            requires ShaderRes<T, ResType>;

        // 获取 shader link 后反射出的 active uniform 列表（无效 ID 返回空表）
        static std::vector<ShaderUniformDesc> get_active_uniforms(const ShaderID& id)
            requires ShaderRes<T, ResType>;

        static TextureID        create(const TextureCreateInfo&         create_info)
            requires TextureRes<T, ResType>;

        static PipelineID       create(const PipelineCreateInfo&        create_info)
            requires PipelineRes<T, ResType>;

        static FrameBufferID    create(const FrameBufferCreateInfo&     create_info)
            requires FrameBufferRes<T, ResType>;

        static UniformBufferID  create(const UniformBufferCreateInfo&   create_info)
            requires UniformBufferRes<T, ResType>;

        // destroy 接口



        static void destroy(const IndexBufferID& id)
            requires IndexBufferRes<T, ResType>;

        static void destroy(const ShaderID& id)
            requires ShaderRes<T, ResType>;

        static void destroy(const TextureID& id)
            requires TextureRes<T, ResType>;

        static void destroy(const PipelineID& id)
            requires PipelineRes<T, ResType>;

        static void destroy(const FrameBufferID& id)
            requires FrameBufferRes<T, ResType>;

        static void destroy(const UniformBufferID& id)
            requires UniformBufferRes<T, ResType>;
    };
} // namespace ID