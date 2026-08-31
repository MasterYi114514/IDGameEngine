#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderGraph/RGTypes.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include <typeindex>

namespace ID
{
    class RenderPassBuilder;
    struct RGPassNode;
    struct RGResourceNode;

    /*
    *   RenderGraph：渲染图 = 节点(Pass) + 边(资源依赖)。
    *   Pass 在 add_pass 时通过 setup() 声明读/写哪些语义资源槽位，
    *   边由图按 RAW/WAW/WAR 规则自动推导，执行顺序由 compile() 拓扑排序决定。
    *   "想让别人读到你的输出，就必须在别人之前声明"——声明时刻决定读到哪个版本。
    */
    class ID_API RenderGraph
    {
    public:
        RenderGraph();
        ~RenderGraph();

        // 禁止拷贝
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        // 允许移动（实现于 .cpp：成员含不完整类型容器）
        RenderGraph(RenderGraph&&);
        RenderGraph& operator=(RenderGraph&&);

    public:
        /**
         *  注册 Pass：构造后立即调用其 setup() 记录依赖。
         *  同类型 Pass 唯一（参考 Component 单例组件模式）：已存在则直接复用并返回已有实例；
         *  注意：声明顺序决定"读到哪个版本"，但不决定执行顺序（缺失的前置依赖会自动补加）
         */
        template<typename PassType, typename... Args>
        PassType& add_pass(Args&&... args)
        {
            static_assert(std::is_base_of<RenderPass, PassType>::value,
                "PassType 必须是 RenderPass 的子类");

            // 同类型唯一：已存在则复用（自动补加的实例也由此被后续显式 add 配置）
            if(RenderPass* existing = find_pass_by_type(std::type_index(typeid(PassType))))
            {
                return static_cast<PassType&>(*existing);
            }

            auto pass = std::make_unique<PassType>(std::forward<Args>(args)...);
            PassType& ref = *pass;
            register_node(std::move(pass));       // 内部调用 setup + 建边 + 前置依赖自动补加，实现在 .cpp
            return ref;                            // 返回值保持现状，调用形式不变
        }

        /** 清空节点与资源时间线（Pass 热插拔 = clear + 重建模式） */
        void clear();

        /** 
         *  @brief 进行 推导校验 + 拓扑排序 + 剔除。
        */
        bool compile();

        /** 
         *  @brief 按拓扑序执行；编译失败则拒绝执行并 ID_ERROR。
         *  底层会判断 m_dirty 标记，并在结构有变动时先 compile()，compile 失败（环等）时拒绝执行
         *  @param ctx 渲染上下文（帧级参数 + 批次列表 + 渲染目标）
         *  @note 仅执行未剔除的 Pass
        */
        void execute(RenderContext& ctx);

        //  查询与调试 
        size_t                          get_pass_count() const;                     // 未剔除的数量
        const std::vector<std::string>& get_execution_order() const { return m_execution_order; } // 编译产物
        bool                            export_graphviz(std::string& out) const;
        RenderPass*                     find_pass_by_type(std::type_index type);                  

        /**
         *  @brief 导出绘制用只读视图（节点编辑器 / 调试 UI 用）
         *  @param out 输出视图；包含全部节点（含剔除）、边及执行序
         */
        void build_view(RGGraphView& out) const;

    private:
        friend class RenderPassBuilder;

        uint32_t register_node(std::unique_ptr<RenderPass> pass);   // setup + 版本建边
        void     derive_edges(RGPassNode& node);                    // RAW/WAW/WAR 规则

        std::vector<RGPassNode>                         m_nodes;          // Pass 节点（声明序）
        std::vector<RGResourceNode>                     m_resources;      // 资源版本时间线（大小 = RGResourceName::Count）
        std::vector<uint32_t>                           m_order;          // 编译后的拓扑序（节点索引）
        std::unordered_map<const RenderPass*, uint32_t> m_pass_to_node;   // pass 指针 → 节点索引（after() 解析用）
        std::vector<std::string>                        m_execution_order; // 编译产物（pass 名列表）
        std::vector<std::type_index>                    m_registering;    // 注册中类型栈（前置依赖自动补加的环防御）
        bool                                            m_dirty = true;    // 结构改动标记
    };
} // namespace ID
