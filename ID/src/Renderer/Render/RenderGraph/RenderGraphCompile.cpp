#include "RenderGraph.hpp"   // 内部头（同目录引号包含，含公开 RenderGraph 类定义）
#include "Log/Log.hpp"

namespace ID
{
    /*
    *   compile：图编译 = Kahn 确定性拓扑排序 + 环检测 + 死 Pass 剔除 + 悬空依赖警告。
    *   编译成功后 m_order 为最终执行序（同层按注册序，同一份装配永远得到同一份执行序）；
    *   失败（存在环）时清空 m_order 并置 m_dirty = false（防止每帧重复报错刷屏），
    *   结构 clear()/重建后会重新编译。
    */
    bool RenderGraph::compile()
    {
        // Kahn 拓扑排序：就绪集合取注册序最小节点 → 确定性调度
        std::vector<uint32_t> order;
        order.reserve(m_nodes.size());

        std::vector<uint32_t> indegree(m_nodes.size());
        for(size_t i = 0; i < m_nodes.size(); ++i)
        {
            m_nodes[i].culled = false;   // 重置剔除标记
            indegree[i] = m_nodes[i].indegree;
        }

        std::set<uint32_t> ready;   // 有序集合：begin() 即注册序最小的就绪节点
        for(size_t i = 0; i < m_nodes.size(); ++i)
        {
            if(indegree[i] == 0) ready.insert(static_cast<uint32_t>(i));
        }

        while(!ready.empty())
        {
            const uint32_t idx = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(idx);

            for(const RGPassEdge& e : m_nodes[idx].succs)
            {
                if(--indegree[e.node] == 0) ready.insert(e.node);
            }
        }

        // 环检测：残留入度 > 0 的节点在环上（或依赖环）→ 拒绝执行
        if(order.size() != m_nodes.size())
        {
            std::string cycle_names;
            for(size_t i = 0; i < m_nodes.size(); ++i)
            {
                if(indegree[i] > 0)
                {
                    if(!cycle_names.empty()) cycle_names += ", ";
                    cycle_names += m_nodes[i].name;
                }
            }
            ID_ERROR("RenderGraph: 检测到依赖环，环上（或依赖环）的 Pass：[{}]，拒绝执行", cycle_names);

            m_order.clear();
            m_execution_order.clear();
            m_dirty = false;   // 防止每帧重复报错；clear()/重建后会重新编译
            return false;
        }

        // 死 Pass 剔除：从最终输出（ViewportTarget 写入者）沿 preds 反向可达性分析
        //     显式 after 边也在 preds 中，"after 链可达的副作用节点"因此自动保留；
        //     ViewportTarget 无写入者时保守跳过剔除（未声明输出的装配不剔除任何 Pass）
        const RGResourceNode& output = m_resources[static_cast<size_t>(RGResource::ViewportTarget)];
        if(output.last_writer >= 0)
        {
            std::vector<uint8_t> reachable(m_nodes.size(), 0);
            std::vector<uint32_t> stack;

            const uint32_t root = static_cast<uint32_t>(output.last_writer);
            reachable[root] = 1;
            stack.push_back(root);

            while(!stack.empty())
            {
                const uint32_t idx = stack.back();
                stack.pop_back();
                for(const RGPassEdge& e : m_nodes[idx].preds)
                {
                    if(!reachable[e.node])
                    {
                        reachable[e.node] = 1;
                        stack.push_back(e.node);
                    }
                }
            }

            std::string culled_names;
            uint32_t culled_count = 0;
            for(size_t i = 0; i < m_nodes.size(); ++i)
            {
                if(!reachable[i])
                {
                    m_nodes[i].culled = true;   // 不可达：对最终输出无贡献

                    // 原因类别：写的资源无人再读 → "输出未被引用"；否则 → "不可达"（副作用隔离）
                    bool output_unreferenced = false;
                    for(const RGHandle& h : m_nodes[i].writes)
                    {
                        const RGResourceNode& res = m_resources[h.slot];
                        if(res.last_writer == static_cast<int32_t>(i) && res.readers.empty())
                        {
                            output_unreferenced = true;
                            break;
                        }
                    }

                    ++culled_count;
                    if(!culled_names.empty()) culled_names += ", ";
                    culled_names += m_nodes[i].name;
                    culled_names += output_unreferenced ? "（输出未被引用）" : "（不可达）";
                }
            }
            if(culled_count > 0)
            {
                ID_INFO("RenderGraph: 剔除对最终输出（ViewportTarget）无贡献的 Pass（{} 个，原因见括号）：[{}]",
                    culled_count, culled_names);
            }
        }

        // 悬空依赖警告：读取的槽位无任何写入者（放在剔除后：被剔除 Pass 不执行，不告警）
        for(const RGPassNode& node : m_nodes)
        {
            if(node.culled) continue;
            for(const RGHandle& h : node.reads)
            {
                const RGResourceNode& res = m_resources[h.slot];
                if(res.last_writer < 0)
                {
                    ID_WARN("RenderGraph: '{}' 读取资源 '{}'，但图中没有 Pass 写入该资源（悬空依赖）",
                        node.name, res.name);
                }
            }
        }

        // 收尾：固化执行序并输出编译日志
        m_order = std::move(order);
        m_execution_order.clear();
        m_execution_order.reserve(m_order.size());
        for(uint32_t idx : m_order)
        {
            if(m_nodes[idx].culled) continue;   // 执行序产物仅保留实际执行的 Pass（剔除名单见上方日志）
            m_execution_order.push_back(m_nodes[idx].name);
        }
        m_dirty = false;

        std::string order_desc;
        for(size_t i = 0; i < m_execution_order.size(); ++i)
        {
            if(!order_desc.empty()) order_desc += " -> ";
            order_desc += std::to_string(i) + "." + m_execution_order[i];
        }
        ID_INFO("RenderGraph: 编译完成，执行序（{} 个 Pass）：[{}]",
            m_execution_order.size(), order_desc);

        return true;
    }
} // namespace ID
