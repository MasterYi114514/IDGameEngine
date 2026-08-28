#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    /**
     *  TransparentPass：透明批次渲染 Pass
     *
     *  从 ForwardPass 中拆分的透明渲染阶段，用于满足正确渲染顺序：
     *      ShadowPass → ForwardPass(不透明) → SkyboxPass → TransparentPass → PostProcessPass
     *  透明物体必须在天空盒之后绘制，否则半透明边缘会混合"清屏色"而非"天空色"。
     *
     *  与 ForwardPass 共享 RenderPassUtil 的绘制逻辑（帧级 uniform + 阴影 uniform）。
     */
    class ID_API TransparentPass : public RenderPass
    {
    public:
        TransparentPass(FrameBufferID output_fb = FrameBufferID::invalid_id(),
            const Vec3& ambient = Vec3(0.15f, 0.15f, 0.15f));
        virtual ~TransparentPass() override = default;

    public:
        void set_output_fb(FrameBufferID fb) { m_output_fb = fb; }
        FrameBufferID get_output_fb() const { return m_output_fb; }

        void set_ambient(const Vec3& ambient) { m_ambient = ambient; }
        const Vec3& get_ambient() const { return m_ambient; }

    public:
        // 声明依赖：读改写 SceneColor（blend 到已有内容，不清屏）；读 ShadowMap（透明物体采样阴影）
        void setup(RenderPassBuilder& builder) override;

        virtual void execute(RenderContext& ctx) override;

    private:
        FrameBufferID  m_output_fb;     // 输出的 FrameBuffer（无效时用 ctx.scene_fb 或默认屏幕）
        Vec3           m_ambient;       // 环境光颜色
    };
} // namespace ID
