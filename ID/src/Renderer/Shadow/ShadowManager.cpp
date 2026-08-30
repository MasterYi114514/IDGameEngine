#include "Renderer/Shadow/ShadowManager.hpp"

#include "BasicPool.hpp"
#include "Log/Log.hpp"

namespace ID
{
    using SMUINT = ShadowMapID::UnderlyingType;
    namespace
    {
        using ShadowMapPool = BasicPool<ShadowMapID::UnderlyingType, ShadowMap>;
        ShadowMapPool g_shadow_map_pool;
    }

    ShadowMapID ShadowManager::create(uint32_t size, uint32_t layer_count, ShadowMapType map_type)
    {
        SMUINT new_id = g_shadow_map_pool.search_slot();
        if(new_id == static_cast<SMUINT>(-1))
        {
            ID_ERROR("[ShadowManager] create: ShadowMapPool 已满，无法创建新的 ShadowMap");
            return ShadowMapID::invalid_id();
        }

        if(new_id >= g_shadow_map_pool.m_pool.size())
        {
            g_shadow_map_pool.m_pool.emplace_back(size, layer_count);
        }
        else
        {
            g_shadow_map_pool.m_pool[new_id] = ShadowMap(size, layer_count);
        }
        g_shadow_map_pool.m_pool[new_id].set_type(map_type);

        ID_TRACE("[ShadowManager] 创建 ShadowMap id={} size={} layers={}", new_id, size, layer_count);

        return ShadowMapID(new_id);
    }

    ShadowMap* ShadowManager::get_shadow_map(ShadowMapID id)
    {
        if (!id.is_valid())
        {
            ID_WARN("[ShadowManager] get_shadow_map: 无效 ID");
            return nullptr;
        }
        return &(g_shadow_map_pool.m_pool[id.get_id()]);
    }

    void ShadowManager::destroy_shadow_map(ShadowMapID id)
    {
        if(!id.is_valid())
        {
            ID_WARN("[ShadowManager] destroy_shadow_map: 无效 ID");
            return;
        }
        
        g_shadow_map_pool.destroy(id.get_id());
    }

} // namespace ID