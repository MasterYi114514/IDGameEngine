#pragma once

namespace ID
{
    struct RenderContext;

    /**
     *  RenderPass：渲染工序，即在渲染过程负责干完某一个任务，比如“渲染不透明物体”、“渲染透明物体”、“渲染光照”等。
     *  RenderPass 本身是一个抽象基类，不进行实例化，子类必须实现 execute(RenderContext&) 方法来完成一次渲染阶段
     */
    class RenderPass
    {
    public:
        RenderPass(const std::string& name = "RenderPass") : m_name(name) { }
        virtual ~RenderPass() = default;

        // 禁止拷贝
        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        // 允许移动
        RenderPass(RenderPass&&) = default;
        RenderPass& operator=(RenderPass&&) = default;

        const std::string& get_name() const { return m_name; }

        // 子类必须实现
        virtual void execute(RenderContext& ctx) = 0;

        virtual void on_begin_frame() { }
        virtual void on_end_frame()   { }

    private:
        std::string m_name;
    };
} // namespace ID