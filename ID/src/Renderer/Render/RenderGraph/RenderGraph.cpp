#include "RenderGraph.hpp"   // 内部头（同目录引号包含，含公开 RenderGraph 类定义）
#include "Log/Log.hpp"

namespace
{
    /*
    *   reset_resources：重置资源版本时间线，并预填四个语义槽位名
    */
    void reset_resources(std::vector<ID::RGResourceNode>& resources)
    {
        resources.assign(static_cast<size_t>(ID::RGResource::Count), ID::RGResourceNode{});
        resources[static_cast<size_t>(ID::RGResource::ShadowMap)].name      = "ShadowMap";
        resources[static_cast<size_t>(ID::RGResource::GBuffer)].name        = "GBuffer";
        resources[static_cast<size_t>(ID::RGResource::SceneColor)].name     = "SceneColor";
        resources[static_cast<size_t>(ID::RGResource::ViewportTarget)].name = "ViewportTarget";
    }

    /*
    *   add_pred：向 node 追加一条前驱边 pred → node（去重，保留首个类型），同步维护 succs 与 indegree
    */
    void add_pred(std::vector<ID::RGPassNode>& nodes, ID::RGPassNode& node, uint32_t index, uint32_t pred,
        ID::RGEdgeType type)
    {
        for(const ID::RGPassEdge& e : node.preds)
        {
            if(e.node == pred) return;
        }
        node.preds.push_back(ID::RGPassEdge{pred, type});
        nodes[pred].succs.push_back(ID::RGPassEdge{index, type});
        ++node.indegree;
    }

    /*
    *   edge_type_name：边类型 → GraphViz 标注名
    */
    const char* edge_type_name(ID::RGEdgeType type)
    {
        switch(type)
        {
            case ID::RGEdgeType::RAW:      return "RAW";
            case ID::RGEdgeType::WAW:      return "WAW";
            case ID::RGEdgeType::WAR:      return "WAR";
            case ID::RGEdgeType::Order:    return "Order";
            case ID::RGEdgeType::Explicit: return "Explicit";
        }
        return "?";
    }

    /*
    *   contains：线性查找（节点数极少，无需哈希）
    */
    bool contains(const std::vector<std::type_index>& list, std::type_index type)
    {
        for(const std::type_index& e : list)
        {
            if(e == type) return true;
        }
        return false;
    }
} // 匿名命名空间

namespace ID
{
    RenderGraph::RenderGraph()
    {
        reset_resources(m_resources);
    }

    RenderGraph::~RenderGraph() = default;

    RenderGraph::RenderGraph(RenderGraph&&) = default;

    RenderGraph& RenderGraph::operator=(RenderGraph&&) = default;

    void RenderGraph::clear()
    {
        m_nodes.clear();
        reset_resources(m_resources);
        m_order.clear();
        m_pass_to_node.clear();
        m_execution_order.clear();
        m_registering.clear();
        m_dirty = true;
    }

    RenderPass* RenderGraph::find_pass_by_type(std::type_index type)
    {
        for(RGPassNode& node : m_nodes)
        {
            if(std::type_index(typeid(*node.pass)) == type)
            {
                return node.pass.get();
            }
        }
        return nullptr;
    }

    uint32_t RenderGraph::register_node(std::unique_ptr<RenderPass> pass)
    {
        m_registering.push_back(std::type_index(typeid(*pass)));   // 类型级环防御：记录注册中状态

        const uint32_t index = static_cast<uint32_t>(m_nodes.size());

        RGPassNode node;
        node.pass = std::move(pass);
        node.name = node.pass->get_name();
        m_nodes.push_back(std::move(node));

        RGPassNode& current = m_nodes[index];

        // 先登记 pass → 节点索引（支持 setup 中 after() 引用已注册节点，含当前节点自身）
        m_pass_to_node[current.pass.get()] = index;

        // 声明阶段：setup 只做依赖声明，RenderPassBuilder 收拢进节点
        RenderPassBuilder builder(*this, index);
        current.pass->setup(builder);

        // ——— 前置依赖解析（参考 Component 前置组件模式）：缺失的依赖自动补加 ———
        // 补加的依赖会物理上插到当前节点之前声明（保证 SSA 版本线正确）；
        // 当前节点采用"撤销占位 → 先注册依赖 → 重新注册自己"的重入方式实现
        bool has_missing = false;
        for(const RGRequireDecl& req : builder.m_requires)
        {
            if(contains(m_registering, req.type) && std::type_index(typeid(*current.pass)) != req.type)
            {
                ID_ERROR("RenderGraph: '{}' 与前置依赖 [{}] 之间存在类型级依赖环，跳过自动补加",
                    current.name, req.type.name());
                continue;
            }
            if(find_pass_by_type(req.type) == nullptr)
            {
                has_missing = true;
            }
        }

        if(has_missing)
        {
            // 撤销当前占位节点（此时尚未建边/未更新版本线，撤销无副作用）
            auto self = std::move(current.pass);
            m_nodes.pop_back();
            m_pass_to_node.erase(self.get());
            m_registering.pop_back();

            for(const RGRequireDecl& req : builder.m_requires)
            {
                if(!contains(m_registering, req.type) && find_pass_by_type(req.type) == nullptr)
                {
                    ID_INFO("RenderGraph: '{}' 的前置依赖 [{}] 未添加，已自动补加（默认构造，可用 add_pass 复用后配置）",
                        self->get_name(), req.type.name());
                    register_node(req.create());   // 递归：依赖的前置依赖同样会被补全
                }
            }
            return register_node(std::move(self));   // 重新注册：setup 重跑，此时依赖已就位
        }

        // 显式 after 边：目标应为已注册节点（可指向前方，环检测在 compile 兜底）
        for(const RenderPass* target : builder.m_after_targets)
        {
            auto it = m_pass_to_node.find(target);
            if(it != m_pass_to_node.end())
            {
                add_pred(m_nodes, current, index, it->second, RGEdgeType::Explicit);
            }
            else
            {
                ID_WARN("RenderGraph: '{}' 的 after() 指向未注册的 Pass '{}'，已忽略",
                    current.name, target->get_name());
            }
        }

        derive_edges(current);

        // 更新资源时间线：写者接管新版本、读者重置；随后登记读者（写者本身不进读者表）
        for(const RGHandle& h : current.writes)
        {
            RGResourceNode& res = m_resources[h.slot];
            res.version     = h.version;   // = 旧版本 + 1（builder 记录时已 +1）
            res.last_writer = static_cast<int32_t>(index);
            res.readers.clear();
        }
        for(const RGHandle& h : current.reads)
        {
            RGResourceNode& res = m_resources[h.slot];
            if(res.last_writer != static_cast<int32_t>(index))
            {
                res.readers.push_back(index);
            }
        }

        m_dirty = true;
        m_registering.pop_back();
        return index;
    }

    void RenderGraph::derive_edges(RGPassNode& node)
    {
        const uint32_t index = static_cast<uint32_t>(&node - m_nodes.data());

        // reads：RAW 边（依赖当前版本写入者）+ 保序边（依赖之前的读者）
        for(const RGHandle& h : node.reads)
        {
            const RGResourceNode& res = m_resources[h.slot];
            if(res.last_writer >= 0)
            {
                add_pred(m_nodes, node, index, static_cast<uint32_t>(res.last_writer), RGEdgeType::RAW);
            }
            for(uint32_t reader : res.readers)
            {
                if(reader != index) add_pred(m_nodes, node, index, reader, RGEdgeType::Order);
            }
        }

        // writes：WAW 边（依赖前一版本写入者）+ WAR 边（不能覆盖别人正在读的数据）
        for(const RGHandle& h : node.writes)
        {
            const RGResourceNode& res = m_resources[h.slot];
            if(res.last_writer >= 0)
            {
                add_pred(m_nodes, node, index, static_cast<uint32_t>(res.last_writer), RGEdgeType::WAW);
            }
            for(uint32_t reader : res.readers)
            {
                if(reader != index) add_pred(m_nodes, node, index, reader, RGEdgeType::WAR);
            }
        }
    }

    void RenderGraph::execute(RenderContext& ctx)
    {
        // 结构有变动时先编译；编译失败（环等）时 compile 已清空 m_order，拒绝执行
        if(m_dirty && !compile())
        {
            return;   // 编译失败：拒绝执行，保留上一帧画面
        }

        for(uint32_t idx : m_order)
        {
            RGPassNode& node = m_nodes[idx];
            if(node.culled) continue;

            node.pass->on_begin_frame();   // 帧钩子（此前从未被调用，补上；均为空实现，安全）
            node.pass->execute(ctx);
            node.pass->on_end_frame();
        }
    }

    bool RenderGraph::export_graphviz(std::string& out) const
    {
        out.clear();
        out += "digraph RenderGraph {\n";
        out += "    rankdir=LR;\n";
        out += "    node [shape=box, fontname=\"Consolas\"];\n";

        // 节点：label = 索引.名称；剔除节点灰显 + 虚线 + culled 标注
        for(size_t i = 0; i < m_nodes.size(); ++i)
        {
            const RGPassNode& node = m_nodes[i];
            out += "    " + std::to_string(i) + " [label=\"" + std::to_string(i) + "." + node.name;
            if(node.culled)
            {
                out += "\\n(culled)\", color=gray, fontcolor=gray, style=dashed";   // DOT label 内 \n 为换行转义
            }
            out += "\"];\n";
        }

        // 边：从 preds 端输出（每条边恰好一次），标注边类型
        for(size_t i = 0; i < m_nodes.size(); ++i)
        {
            for(const RGPassEdge& e : m_nodes[i].preds)
            {
                out += "    " + std::to_string(e.node) + " -> " + std::to_string(i)
                    + " [label=\"" + edge_type_name(e.type) + "\"];\n";
            }
        }

        out += "}\n";
        return true;
    }

    size_t RenderGraph::get_pass_count() const
    {
        size_t count = 0;
        for(const RGPassNode& node : m_nodes)
        {
            if(!node.culled) ++count;
        }
        return count;
    }

    void RenderGraph::build_view(RGGraphView& out) const
    {
        out.passes.clear();
        out.edges.clear();
        out.execution_order = m_execution_order;

        // 槽位名："ShadowMap" 等，下标 = RGResource
        out.resource_names.clear();
        for(const RGResourceNode& res : m_resources)
        {
            out.resource_names.push_back(res.name);
        }

        // exec_index 反查表：m_order 中的位置即执行序；未入执行序（被剔除/编译失败清空） = -1
        std::vector<int32_t> exec_index_of(m_nodes.size(), -1);
        for(size_t i = 0; i < m_order.size(); ++i)
        {
            exec_index_of[m_order[i]] = static_cast<int32_t>(i);
        }

        for(size_t i = 0; i < m_nodes.size(); ++i)
        {
            const RGPassNode& node = m_nodes[i];

            RGPassView view;
            view.index      = static_cast<uint32_t>(i);
            view.exec_index = exec_index_of[i];
            view.name       = node.name;
            view.culled     = node.culled;

            // reads / writes：RGHandle.slot 去重后转 RGResource（同槽位多次声明只保留一次）
            for(const RGHandle& h : node.reads)
            {
                if(!h.is_valid()) continue;
                const RGResource res = static_cast<RGResource>(h.slot);
                if(std::find(view.reads.begin(), view.reads.end(), res) == view.reads.end())
                {
                    view.reads.push_back(res);
                }
            }
            for(const RGHandle& h : node.writes)
            {
                if(!h.is_valid()) continue;
                const RGResource res = static_cast<RGResource>(h.slot);
                if(std::find(view.writes.begin(), view.writes.end(), res) == view.writes.end())
                {
                    view.writes.push_back(res);
                }
            }

            out.passes.push_back(std::move(view));

            // 边：从 preds 端输出（与 export_graphviz 同源数据）
            for(const RGPassEdge& e : node.preds)
            {
                out.edges.push_back(RGEdgeView{ e.node, static_cast<uint32_t>(i), e.type });
            }
        }
    }
} // namespace ID
