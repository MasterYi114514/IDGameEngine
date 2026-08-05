#pragma once

#include "IDpch.hpp"
#include "Renderer/RenderPass/RenderPass.hpp"
#include "Renderer/RenderPass/RenderPassContext.hpp"

namespace ID
{
    /**
     *  按照注册顺序执行 Pass
     */
    class ID_API RenderGraph
    {
    public:
        RenderGraph();
        ~RenderGraph();

        // 禁止拷贝
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        // 允许移动
        RenderGraph(RenderGraph&&) = default;
        RenderGraph& operator=(RenderGraph&&) = default;

    public:
        // 注册 Pass
        template<typename PassType, typename... Args>
        PassType& add_pass(Args&&... args)
        {
            static_assert(std::is_base_of<RenderPass, PassType>::value,
                "PassType 必须是 RenderPass 的子类");

            auto pass = std::make_unique<PassType>(std::forward<Args>(args)...);
            PassType& ref = *pass;
            m_passes.push_back(std::move(pass));
            return ref;
        }

        void clear();
        size_t get_pass_count() const { return m_passes.size(); }

        void execute(RenderContext& ctx);

    private:
        std::vector<std::unique_ptr<RenderPass>> m_passes;
    };
} // namespace ID