#include "ArenaManager.hpp"
#include "Arena.hpp"

#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace
{
    std::vector<ID::Arena> g_ArenaPool;
    std::unordered_set<ID::ArenaIDType> g_FreedArenaIDs;

    ID::ArenaIDType search_slot()
    {
        ID::ArenaIDType new_id = static_cast<ID::ArenaIDType>(-1); // 默认返回无效 ID

        if(!g_FreedArenaIDs.empty())
        {
            auto it = g_FreedArenaIDs.begin();
            new_id = *it;
            g_FreedArenaIDs.erase(it);
        }
        else
        {
            new_id = g_ArenaPool.size();
        }

        return new_id;
    }

    void destroy(ID::ArenaIDType id)
    {
        if(id < g_ArenaPool.size())
        {
            g_ArenaPool[id].destroy();
            g_FreedArenaIDs.insert(id);
        }
    }
} // 匿名命名空间

namespace ID
{
    ArenaID ArenaManager::create_arena(size_t size)
    {
        ArenaIDType new_id = search_slot();

        if(new_id == static_cast<ArenaIDType>(-1))
        {
            return ArenaID::invalid_id();
        }

        if(new_id >= g_ArenaPool.size())
        {
            g_ArenaPool.emplace_back(size);
        }
        else
        {
            g_ArenaPool[new_id] = Arena(size);
        }

        return ArenaID{new_id};
    }

    void ArenaManager::destroy_arena(ArenaID arena_id)
    {
        destroy(arena_id.m_id);
    }

    void ArenaManager::reset_arena(ArenaID arena_id)
    {
        if(arena_id.m_id < g_ArenaPool.size())
        {
            g_ArenaPool[arena_id.m_id].reset();
        }
    }

    // 获取 Arena 实例的引用
    Arena& get_arena(ArenaID arena_id)
    {
        if(arena_id.get_id() >= g_ArenaPool.size())
        {
            throw std::out_of_range("ArenaID 超出范围，无法获取 Arena 实例");
        }
        return g_ArenaPool[arena_id.get_id()];
    }
}