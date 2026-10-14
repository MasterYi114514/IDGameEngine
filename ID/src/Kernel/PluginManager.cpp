#include "Kernel/Plugin.hpp"
#include "Kernel/BootstrapLog.hpp"

#include <cstdio>

namespace ID
{
    // 显式特化定义：在微内核数据段中以 constinit 零开销初始化唯一实例
    template <>
    ID_KERNEL_API constinit PluginManager SingletonEager<PluginManager>::s_instance{};

    int32_t PluginManager::find_plugin_index(uint64_t plugin_id) const noexcept
    {
        for (size_t i = 0; i < m_registered_count; ++i)
        {
            if (m_registered[i]->plugin_id == plugin_id)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    bool PluginManager::register_plugin(const PluginDescriptor* descriptor) noexcept
    {
        ID_KERNEL_ASSERT(descriptor != nullptr, "Cannot register a null PluginDescriptor!");
        ID_KERNEL_ASSERT(!m_is_initialized, "Cannot register plugins after initialize_all()!");

        if (m_registered_count >= kMaxPlugins) [[unlikely]]
        {
            ID_KERNEL_LOG(Log::Level::Error, "Exceeded maximum plugin capacity (128)!");
            return false;
        }

        if (find_plugin_index(descriptor->plugin_id) >= 0) [[unlikely]]
        {
            ID_KERNEL_LOG(Log::Level::Error, "Duplicate plugin_id registration detected!");
            return false;
        }

        m_registered[m_registered_count++] = descriptor;
        return true;
    }

    bool PluginManager::initialize_all() noexcept
    {
        if (m_registered_count == 0)
        {
            m_is_initialized = true;
            return true;
        }

        // 1. 校验所有依赖项是否存在且版本满足要求，同时计算每个插件节点的入度 (in_degree)
        uint32_t in_degree[kMaxPlugins] = { 0 };

        for (size_t i = 0; i < m_registered_count; ++i)
        {
            const PluginDescriptor* desc = m_registered[i];
            for (const PluginDependency& dep : desc->dependencies)
            {
                int32_t dep_idx = find_plugin_index(dep.plugin_id);
                if (dep_idx < 0) [[unlikely]]
                {
                    char msg[256];
                    std::snprintf(msg, sizeof(msg), "Plugin '%.*s' is missing required dependency (ID: 0x%llX)!",
                        static_cast<int>(desc->name.size()), desc->name.data(), static_cast<unsigned long long>(dep.plugin_id));
                    ID_KERNEL_LOG(Log::Level::Fatal, msg);
                    return false;
                }
                if (m_registered[dep_idx]->version < dep.min_version) [[unlikely]]
                {
                    ID_KERNEL_LOG(Log::Level::Fatal, "Plugin dependency version mismatch!");
                    return false;
                }
                ++in_degree[i];
            }
        }

        // 2. Kahn 拓扑排序：将所有入度为 0（无前置依赖）的插件压入定长队列
        size_t queue[kMaxPlugins] = { 0 };
        size_t q_head = 0;
        size_t q_tail = 0;

        for (size_t i = 0; i < m_registered_count; ++i)
        {
            if (in_degree[i] == 0)
            {
                queue[q_tail++] = i;
            }
        }

        size_t sorted_count = 0;
        while (q_head < q_tail)
        {
            size_t u_idx = queue[q_head++];
            const PluginDescriptor* u_desc = m_registered[u_idx];
            m_topo_order[sorted_count++] = u_desc;

            // 遍历所有依赖了 u_desc 的下游插件 v，将其入度减 1
            for (size_t v_idx = 0; v_idx < m_registered_count; ++v_idx)
            {
                if (in_degree[v_idx] == 0)
                {
                    continue;
                }
                const PluginDescriptor* v_desc = m_registered[v_idx];
                for (const PluginDependency& dep : v_desc->dependencies)
                {
                    if (dep.plugin_id == u_desc->plugin_id)
                    {
                        if (--in_degree[v_idx] == 0)
                        {
                            queue[q_tail++] = v_idx;
                        }
                        break;
                    }
                }
            }
        }

        // 3. 环路检测：若拓扑序列长度小于注册插件总数，说明存在循环依赖
        if (sorted_count != m_registered_count) [[unlikely]]
        {
            ID_KERNEL_LOG(Log::Level::Fatal, "Cyclic dependency detected in plugin DAG!");
            for (size_t i = 0; i < m_registered_count; ++i)
            {
                if (in_degree[i] > 0)
                {
                    ID_KERNEL_LOG(Log::Level::Fatal, m_registered[i]->name);
                }
            }
            return false;
        }

        // 4. 阶段 1 (拓扑正序)：执行所有插件的 on_register
        for (size_t i = 0; i < sorted_count; ++i)
        {
            if (m_topo_order[i]->on_register)
            {
                m_topo_order[i]->on_register();
            }
        }

        // 5. 阶段 2 (拓扑正序)：执行所有插件的 on_init
        m_initialized_count = 0;
        for (size_t i = 0; i < sorted_count; ++i)
        {
            const PluginDescriptor* desc = m_topo_order[i];
            if (desc->on_init && !desc->on_init()) [[unlikely]]
            {
                ID_KERNEL_LOG(Log::Level::Fatal, "Plugin on_init() failed! Rolling back initialized plugins...");
                shutdown_all();
                return false;
            }
            ++m_initialized_count;
        }

        m_is_initialized = true;
        return true;
    }

    void PluginManager::shutdown_all() noexcept
    {
        // 阶段 3 (严格拓扑逆序)：从最上层业务插件开始依次执行 on_shutdown
        for (size_t i = m_initialized_count; i > 0; --i)
        {
            const PluginDescriptor* desc = m_topo_order[i - 1];
            if (desc->on_shutdown)
            {
                desc->on_shutdown();
            }
        }

        // 阶段 4 (严格拓扑逆序)：依次执行 on_unload 注销服务表
        for (size_t i = m_initialized_count; i > 0; --i)
        {
            const PluginDescriptor* desc = m_topo_order[i - 1];
            if (desc->on_unload)
            {
                desc->on_unload();
            }
        }

        m_initialized_count = 0;
        m_registered_count  = 0;
        m_is_initialized    = false;
    }
} // namespace ID
