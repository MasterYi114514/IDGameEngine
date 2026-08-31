#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderGraph/RGTypes.hpp"
#include <typeindex>

namespace ID
{
    class RenderGraph;
    class RenderPass;

    /*
    *   RGRequireDecl：前置依赖 Pass 声明项（参考 Component 前置组件模式）。
    *   type 记录依赖的 Pass 类型；create 为默认构造闭包，供缺失时自动补加。
    */
    struct RGRequireDecl
    {
        std::type_index                                type;      // 依赖的 Pass 类型
        std::function<std::unique_ptr<RenderPass>()>   create;    // 默认构造闭包（自动补加用）
    };

    /**
    *   RenderPassBuilder：Pass 声明构建器。
    *   仅在 RenderGraph::add_pass 内部构造（构造函数私有，友元 RenderGraph），
    *   传给 RenderPass::setup() 声明本 Pass 读/写哪些资源；声明收拢后写入图节点。
    *   @warning 禁止在 setup 中做任何 GPU 操作——只做依赖声明。
    *   "想让别人读到你的输出，就必须在别人之前声明"（版本时间线语义）。
    */
    class ID_API RenderPassBuilder
    {
    public:
        /** 读取资源（RAW 边：依赖该资源当前版本的写入者） */
        void reads(RGResourceName res);

        /** 写入资源（WAW 边：依赖前一版本写入者；WAR 边：依赖之前的读者），版本 +1 */
        void writes(RGResourceName res);

        /** 读改写（= reads + writes），如 Skybox/Transparent 对 SceneColor */
        void read_writes(RGResourceName res);

        /** 显式顺序依赖（逃生舱：无资源关系的副作用排序，如 "Clear 必须最先"） */
        void after(const RenderPass& target);

        /**
         *  声明前置依赖 Pass（参考 Component 前置组件模式）：
         *  若图中未添加该类型 Pass，则自动补加（默认构造，物理上插到当前 Pass 之前声明，保证版本线正确）；
         *  仅用于硬依赖（缺失则渲染错误）；可选增强（如 Forward 之于 Shadow）不要声明，
         *  否则开关语义失效（悬空警告已覆盖提示）。
         */
        template<typename PassType>
        void requires_pass()
        {
            static_assert(std::is_base_of<RenderPass, PassType>::value,
                "PassType 必须是 RenderPass 的子类");
            static_assert(std::is_default_constructible<PassType>::value,
                "被依赖的 Pass 必须可默认构造（自动补加用）");
            m_requires.push_back(RGRequireDecl{
                std::type_index(typeid(PassType)),
                [] { return std::unique_ptr<RenderPass>(std::make_unique<PassType>()); }
            });
        }

        // ——— Phase 4b 预留：瞬态资源声明 ———
        // RGHandle create_texture(const RGTextureDesc& desc);

    private:
        friend class RenderGraph;   // 仅 RenderGraph::register_node 可构造/收拢

        RenderPassBuilder(RenderGraph& graph, uint32_t node_index);

        RenderGraph&                   m_graph;          // 所属图
        uint32_t                       m_node_index = 0xFFFFFFFF;   // 当前节点索引
        std::vector<const RenderPass*> m_after_targets;  // 显式顺序依赖目标（register_node 时解析为边）
        std::vector<RGRequireDecl>     m_requires;       // 前置依赖声明（register_node 时解析/自动补加）
    };
} // namespace ID
