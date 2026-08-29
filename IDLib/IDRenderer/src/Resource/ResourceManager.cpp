#include "Resource/ResourceManager.hpp"
#include "Resource/ResourceGetter.hpp"

#include "Log/Log.hpp"
#include "Resource/VertexBuffer/VertexBuffer.hpp"
#include "Resource/IndexBuffer/IndexBuffer.hpp"
#include "Resource/Shader/Shader.hpp"
#include "Resource/Texture/Texture.hpp"
#include "Resource/Pipeline/Pipeline.hpp"
#include "Resource/FrameBuffer/FrameBuffer.hpp"
#include "Resource/UniformBuffer/UniformBuffer.hpp"

namespace
{
    template<std::unsigned_integral ResUINT, typename Res>
    struct ResourcePool
    {
        std::vector<Res>            m_pool;
        std::unordered_set<ResUINT> m_freed_ids;     // 用于存储已销毁资源的 ID，避免重复使用
    };

    ResourcePool<ID::VertexBufferUINT, ID::VertexBuffer>    g_VBPool;
    ResourcePool<ID::IndexBufferUINT, ID::IndexBuffer>      g_IBPool;
    ResourcePool<ID::ShaderUINT, ID::Shader>                g_ShaderPool;
    ResourcePool<ID::TextureUINT, ID::Texture>              g_TexturePool;
    ResourcePool<ID::PipelineUINT, ID::Pipeline>            g_PipelinePool;
    ResourcePool<ID::FrameBufferUINT, ID::FrameBuffer>      g_FBPool;
    ResourcePool<ID::UniformBufferUINT, ID::UniformBuffer>  g_UBPool;

#define SLOT_IMPL(res_type, res_pool)                       \
    if constexpr (ResType == ID::ResourceType::res_type)    \
    {                                                       \
        new_id = res_pool.m_pool.size();                    \
        if(!res_pool.m_freed_ids.empty())                   \
        {                                                   \
            auto it = res_pool.m_freed_ids.begin();         \
            new_id = *it;                                   \
            res_pool.m_freed_ids.erase(it);                 \
        }                                                   \
    }

    template<std::unsigned_integral ResUINT, ID::ResourceType ResType>
    ResUINT search_slot()
    {
        ResUINT new_id = static_cast<ResUINT>(-1);          // 默认返回无效 ID
        
        SLOT_IMPL(VertexBuffer, g_VBPool);
        SLOT_IMPL(IndexBuffer,  g_IBPool);
        SLOT_IMPL(Shader,       g_ShaderPool);
        SLOT_IMPL(Texture,      g_TexturePool);
        SLOT_IMPL(Pipeline,     g_PipelinePool);
        SLOT_IMPL(FrameBuffer,  g_FBPool);
        SLOT_IMPL(UniformBuffer, g_UBPool);

        return new_id;
    }

#define DESTROY_IMPL(res_type, res_pool)                    \
    if constexpr (ResType == ID::ResourceType::res_type)    \
    {                                                       \
        if(id < res_pool.m_pool.size())                     \
        {                                                   \
            res_pool.m_freed_ids.insert(id);                \
            res_pool.m_pool[id].destroy();                  \
        }                                                   \
        else                                                \
        {                                                   \
            IDR_ERROR("在 " #res_type " 资源池中传入超出 " #res_type "Pool 大小的 id，这是不应该发生的情况，可能是资源管理器的逻辑出现了问题");                              \
        }                                                   \
    }

    template<std::unsigned_integral ResUINT, ID::ResourceType ResType>
    void destroy_impl(ResUINT id)
    {
        DESTROY_IMPL(VertexBuffer,  g_VBPool);
        DESTROY_IMPL(IndexBuffer,   g_IBPool);
        DESTROY_IMPL(Shader,        g_ShaderPool);
        DESTROY_IMPL(Texture,       g_TexturePool);
        DESTROY_IMPL(Pipeline,      g_PipelinePool);
        DESTROY_IMPL(FrameBuffer,   g_FBPool);
        DESTROY_IMPL(UniformBuffer, g_UBPool);
    }
} // 匿名命名空间

namespace ID::ResourceGetter
{

#define GET_IMPL(res_type, res_pool)                            \
    if(id.is_valid())                                           \
    {                                                           \
        auto raw_id = id.get_id();                              \
        if(raw_id < res_pool.m_pool.size())                     \
        {                                                       \
            return &res_pool.m_pool[raw_id];                    \
        }                                                       \
        else                                                    \
        {                                                       \
            IDR_ERROR("在 " #res_type " 资源池中传入超出 " #res_type "Pool 大小的 id，这是不应该发生的情况，可能是资源管理器的逻辑出现了问题");                                  \
            return nullptr;                                     \
        }                                                       \
    }                                                           \
    else                                                        \
    {                                                           \
        IDR_ERROR("传入了无效的 " #res_type "ID，无法获取对应的 " #res_type " 对象");                                                     \
        return nullptr;                                         \
    }
    
    VertexBuffer*   get_vertex_buffer   (const VertexBufferID   id)
    {
        GET_IMPL(VertexBuffer,  g_VBPool);
    }

    IndexBuffer*    get_index_buffer    (const IndexBufferID    id)
    {
        GET_IMPL(IndexBuffer,   g_IBPool);
    }

    Shader*         get_shader          (const ShaderID         id)
    {
        GET_IMPL(Shader,        g_ShaderPool);
    }

    Texture*        get_texture         (const TextureID        id)
    {
        GET_IMPL(Texture,       g_TexturePool);
    }

    Pipeline*       get_pipeline        (const PipelineID       id)
    {
        GET_IMPL(Pipeline,      g_PipelinePool);
    }

    FrameBuffer*    get_frame_buffer    (const FrameBufferID    id)
    {
        GET_IMPL(FrameBuffer,   g_FBPool);
    }

    UniformBuffer*  get_uniform_buffer  (const UniformBufferID  id)
    {
        GET_IMPL(UniformBuffer, g_UBPool);
    }
} // namespace ID::ResourceGetter

namespace ID
{
#define CREATE_IMPL(res_type, res_pool)                                                     \
    template<>                                                                              \
    IDR_API res_type##ID ResourceManager<res_type##UINT, ResourceType::res_type>::create(const res_type##CreateInfo& create_info)                                                                            \
    {                                                                                       \
        auto new_id = search_slot<res_type##UINT, ResourceType::res_type>();                \
                                                                                            \
        if(res_type##ID::invalid_id() == new_id)                                            \
        {                                                                                   \
            IDR_WARN(#res_type " 资源池已满，请选择更大的 " #res_type "UINT 类型来创建资源管理器"); \
            return res_type##ID::invalid_id();                                               \
        }                                                                                   \
                                                                                            \
        if(new_id >= res_pool.m_pool.size())                                                \
        {                                                                                   \
            res_pool.m_pool.emplace_back(create_info);                                      \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            res_pool.m_pool[new_id] = std::move(res_type(create_info));                     \
        }                                                                                   \
                                                                                            \
        IDR_INFO("成功创建id为 {} 的 "#res_type" 资源", new_id);                               \
        return res_type##ID{new_id};                                                        \
    }

    CREATE_IMPL(VertexBuffer,   g_VBPool);
    CREATE_IMPL(IndexBuffer,    g_IBPool);
    CREATE_IMPL(Shader,         g_ShaderPool);
    CREATE_IMPL(Texture,        g_TexturePool);
    CREATE_IMPL(Pipeline,       g_PipelinePool);
    CREATE_IMPL(FrameBuffer,    g_FBPool);
    CREATE_IMPL(UniformBuffer,  g_UBPool);

#define DESTROY_RES_IMPL(res_type, res_pool)                                                    \
    template<>                                                                                  \
    IDR_API void ResourceManager<res_type##UINT, ResourceType::res_type>::destroy(const res_type##ID& id) \
    {                                                                                           \
        destroy_impl<res_type##UINT, ResourceType::res_type>(id.get_id());                      \
        IDR_INFO("已销毁id为 {} 的 "#res_type" 资源", id.get_id());                                \
    }

    DESTROY_RES_IMPL(VertexBuffer,  g_VBPool);
    DESTROY_RES_IMPL(IndexBuffer,   g_IBPool);
    DESTROY_RES_IMPL(Shader,        g_ShaderPool);
    DESTROY_RES_IMPL(Texture,       g_TexturePool);
    DESTROY_RES_IMPL(Pipeline,      g_PipelinePool);
    DESTROY_RES_IMPL(FrameBuffer,   g_FBPool);
    DESTROY_RES_IMPL(UniformBuffer, g_UBPool);
    
    // VertexBuffer 其它接口
    template<>
    IDR_API uint32_t ResourceManager<VertexBufferUINT, ResourceType::VertexBuffer>::get_vertex_count(const VertexBufferID& id)
    {
        return IDR_ResVB(id)->get_vertex_count();
    }

    // Shader 其它接口：返回 shader 反射出的 active uniform 列表拷贝（列表很小，拷贝可接受）
    template<>
    IDR_API std::vector<ShaderUniformDesc> ResourceManager<ShaderUINT, ResourceType::Shader>::get_active_uniforms(const ShaderID& id)
    {
        Shader* shader = IDR_ResShader(id);
        if(!shader)
        {
            IDR_ERROR("传入无效的 ShaderID，无法获取 active uniform 列表");
            return {};
        }
        return shader->get_active_uniforms();
    }
} // namespace ID
