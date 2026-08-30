#pragma once

#include "Core/IDRpch.hpp"

namespace ID
{
    template<std::unsigned_integral T, ResourceType ResType>
    class ResourceManager;

    /*
    *   由于 ResourceID 底层的 T 往往只占用 4 个字节乃至更少
    *   需要使用 ResourceID 来传参的时候，`const ***ID` 比 `const ***ID&` 更建议
    */
    template<std::unsigned_integral T, ResourceType ResType>
    class ResourceID
    {
        friend class ResourceManager<T, ResType>;
    public:
        // 默认构造（获取无效的 ResourceID）
        ResourceID() : m_id(static_cast<T>(-1)) { }
        ~ResourceID() = default;

        // 拷贝操作
        ResourceID(const ResourceID& other) = default;
        ResourceID& operator=(const ResourceID& other) = default;

        // 移动操作
        ResourceID(ResourceID&& other) noexcept = default;
        ResourceID& operator=(ResourceID&& other) noexcept = default;

        static constexpr ResourceID invalid_id() { return ResourceID(static_cast<T>(-1)); }
        bool is_valid() const { return m_id != static_cast<T>(-1); }

        static constexpr ResourceType get_resource_type() { return ResType; }

        // 获取原始 id
        T get_id() const { return m_id; }

        // 提供比较操作符
        bool operator==(const ResourceID& other) const { return m_id == other.m_id; }
        bool operator!=(const ResourceID& other) const { return m_id != other.m_id; }
        bool operator==(const T& id) const { return m_id == id; }
        bool operator!=(const T& id) const { return m_id != id; }

    private:
        // 私有构造函数，仅允许友元类 ResourceManager 创建 ResourceID 对象
        ResourceID(T id) : m_id(id) { }

        T m_id;
    };

    using VertexBufferID = ResourceID<VertexBufferUINT, ResourceType::VertexBuffer>;
    using IndexBufferID = ResourceID<IndexBufferUINT, ResourceType::IndexBuffer>;
    using ShaderID = ResourceID<ShaderUINT, ResourceType::Shader>;
    using PipelineID = ResourceID<PipelineUINT, ResourceType::Pipeline>;
    using TextureID = ResourceID<TextureUINT, ResourceType::Texture>;
    using FrameBufferID = ResourceID<FrameBufferUINT, ResourceType::FrameBuffer>;
    using SamplerID = ResourceID<SamplerUINT, ResourceType::Sampler>;
    using UniformBufferID = ResourceID<UniformBufferUINT, ResourceType::UniformBuffer>;
} // namespace ID