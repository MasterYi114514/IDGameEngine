#pragma once

#include "Core/IDCore.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    class RenderPassBuilder;   // 前置声明（声明构建器，避免公开头引入完整定义依赖）

    /**
     *  RenderPass：渲染工序，即在渲染过程负责干完某一个任务，比如“渲染不透明物体”、“渲染透明物体”、“渲染光照”等。
     *  RenderPass 本身是一个抽象基类，不进行实例化，子类必须实现 execute(RenderContext&) 方法来完成一次渲染阶段
     */
    class ID_API RenderPass
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

        /**
         *  声明阶段：声明本 Pass 读/写哪些资源（在 add_pass 时由 RenderGraph 调用）
         *  禁止在此做任何 GPU 操作——只做依赖声明
         */
        virtual void setup(RenderPassBuilder& builder) { /* 默认无资源依赖 */ }

        virtual void on_begin_frame() { }
        virtual void on_end_frame()   { }

    public:
        /**
         *  batch 指提交条目的批次，即一次 draw call 画的一组物体
         */
        static void draw_batch(RenderContext& ctx, const ModelSE& entry, Vec3 ambient);

        // 帧级 uniform（view / proj / camera_pos / ambient / time / lights）的设置
        static void set_frame_uniforms(RenderContext& ctx, ShaderID shader, Vec3 ambient);

        // 物体级 uniform（MVP / u_model）的设置
        static void set_object_uniforms(RenderContext& ctx, ShaderID shader, const ModelSE& entry);

        /**
         *  ctx.shadow_enabled 为 true 时，表示当前帧启用了阴影渲染
         */
        static void apply_shadow(RenderContext& ctx, ShaderID shader);

    private:
        std::string m_name;
    };
} // namespace ID