#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    /**
     *  ForwardPass：前向渲染工序，把每个物体按照不透明批次（pipeline 分组 + 近→远）和透明批次（远→近）渲染到输出 FB 上
     *  会预先给每个物体都计算环境光
     *  - `FB选择`：
     *    - 默认情况下，m_output_fb > ctx.scene_fb > default_fb
     *    - 可以通过设置 `m_use_scene_fb` 当 m_output_fb 无效时使用 ctx.scene_fb 作为输出
     *  - 可以通过设置 `m_render_transparent` 来控制是否渲染透明物体
     *  - 会在渲染时丢弃超过 MAX_LIGHTS 的光源
     */
    class ID_API ForwardPass : public RenderPass
    {
    public:
        // 最多支持的光源数，会在渲染时丢弃超出部分
        static constexpr uint32_t MAX_LIGHTS = 8;

        ForwardPass(FrameBufferID output_fb = FrameBufferID::invalid_id(),
            const Vec3& ambient = Vec3(0.15f, 0.15f, 0.15f));
        virtual ~ForwardPass() override = default;

    public:
        void set_output_fb(FrameBufferID fb) { m_output_fb = fb; }
        FrameBufferID get_output_fb() const { return m_output_fb; }

        void set_ambient(const Vec3& ambient) { m_ambient = ambient; }
        const Vec3& get_ambient() const { return m_ambient; }

    public:
        /**
         *  ForwardPass 的渲染阶段：
         *  - 绑定输出 FB（m_output_fb 或 ctx.scene_fb）
         *  - 设置 viewport 为 ctx.window_width / ctx.window_height
         *  - 清屏
         *  - 渲染不透明批次（ctx.opaque_batches）
         *  - 如果 m_render_transparent 为 true，渲染透明批次（ctx.transparent_batches）
         */
        virtual void execute(RenderContext& ctx) override;

    public:
        // 状态控制

        bool is_use_scene_fb() const { return m_use_scene_fb; }
        void set_use_scene_fb(bool use) { m_use_scene_fb = use; }

        bool is_render_transparent() const { return m_render_transparent; }
        void set_render_transparent(bool render) { m_render_transparent = render; }

    private:
        // 输出的 FrameBuffer
        FrameBufferID  m_output_fb = FrameBufferID::invalid_id();     
        Vec3           m_ambient;               // 环境光颜色
        bool           m_use_scene_fb;          // 是否使用 ctx.scene_fb 作为输出
        bool           m_render_transparent;    // 是否渲染透明物体
    };
} // namespace ID