#pragma once

#include <cstdint>
#include <concepts>

#include "Log/Log.hpp"

namespace ID
{
    template<std::unsigned_integral IDType, typename T>
    struct BasicPool
    {
        std::vector<T>                  m_pool;
        std::unordered_set<IDType>      m_freed_ids;    // 用于存储已销毁资源的 ID，避免重复使用

        IDType search_slot();
        void destroy(IDType id);
    };

    template<std::unsigned_integral IDType, typename T>
    IDType BasicPool<IDType, T>::search_slot()
    {
        IDType new_id = static_cast<IDType>(-1);          // 默认返回无效 ID
        
        new_id = m_pool.size();
        if(!m_freed_ids.empty())
        {
            auto it = m_freed_ids.begin();
            new_id = *it;
            m_freed_ids.erase(it);
        }

        return new_id;
    }

    template<std::unsigned_integral IDType, typename T>
    void BasicPool<IDType, T>::destroy(IDType id)
    {
        if(id < m_pool.size())
        {
            m_freed_ids.insert(id);
            m_pool[id].destroy();
        }
        else
        {
            ID_ERROR("在 BasicPool 中传入超出 Pool 大小的 id，这是不应该发生的情况，请检索代码逻辑问题");
        }
    }
} // namespace ID