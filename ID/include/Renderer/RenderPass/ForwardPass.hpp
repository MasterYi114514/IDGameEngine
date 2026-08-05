#pragma once

#include "IDpch.hpp"
#include "Renderer/RenderPass/RenderPass.hpp"
#include "Renderer/RenderPass/RenderPassContext.hpp"

namespace ID
{
    /**
     *  ForwardPass：前向渲染工序，把每个物体按照不透明批次（pipeline 分组 + 近→远）和透明批次（远→近）渲染到输出 FBO 上
     *  会预先给每个物体都计算光照
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
        virtual void execute(RenderContext& ctx) override;

    private:
        /**
         *  batch 指提交条目的批次，即一次 draw call 画的一组物体
         *  
         */
        void draw_batch(RenderContext& ctx, const SubmitEntry& entry);

        // 帧级 uniform（view / proj / camera_pos / ambient / time / lights）
        void set_frame_uniforms(RenderContext& ctx, ShaderID shader);

        // 物体级 uniform（MVP / u_model）
        void set_object_uniforms(RenderContext& ctx, ShaderID shader, const SubmitEntry& entry);

    private:
        FrameBufferID  m_output_fb;     // 输出的 FrameBuffer
        Vec3           m_ambient;       // 环境光颜色
    };
} // namespace ID