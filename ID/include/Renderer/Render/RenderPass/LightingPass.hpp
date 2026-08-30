#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    /**
     *  LightingPass：延迟渲染光照阶段（全屏三角形）
     *  采样 G-Buffer 三个附件重组 per-pixel 输入，逐光源光照 + 阴影，输出 HDR SceneColor；
     *  并把 G-Buffer 深度 blit 到场景 FBO（供后续 Skybox/Transparent 深度测试）。
     *  - 光源上限 MAX_LIGHTS = 32（延迟路径较前向 8 提升；与 deferred_lighting.fsl 数组长度一致）
     *  - 光照/阴影公式与 geometry.fsl（前向）逐段一致，两路径画面互为验收基准
     *  - 阴影：array 纹理绑 slot 3/4（raw+cmp），ShadowBlock UBO（binding 0）由 ShadowPass 上传，
     *    本 Pass 仅 bind + 补设 u_view（CSM 选层需要主相机 view，延迟路径无 vs 提供）
     */
    class ID_API LightingPass : public RenderPass
    {
    public:
        // 光源上限：与 assets/shader/deferred_lighting.fsl 的 u_light_dirs[32] 等数组长度一一对应
        static constexpr uint32_t MAX_LIGHTS = 32;

        LightingPass(const Vec3& ambient = Vec3(1.0f, 1.0f, 1.0f));
        virtual ~LightingPass() override = default;

    public:
        // 声明依赖：硬依赖 GBufferPass（没有几何数据光照无从谈起）；读 GBuffer + ShadowMap；写 SceneColor
        void setup(RenderPassBuilder& builder) override;

        /**
         *  LightingPass 的渲染阶段：
         *  - 绑定 ctx.scene_fb + viewport + 清屏（color + depth）
         *  - blit G-Buffer 深度 → 场景 FBO（供 Skybox/Transparent 深度测试）
         *  - 绑定 G-Buffer 三附件 → slot 0/1/2；阴影贴图 → slot 3
         *  - 设置相机/环境光/光源数组 uniform，绘制全屏三角形
         */
        virtual void execute(RenderContext& ctx) override;

    private:
        // 懒创建：deferred_lighting shader + 全屏三角形管线（深度测试/写入/混合全关，cull=None）
        void ensure_resources();

    private:
        Vec3           m_ambient = Vec3(1.0f, 1.0f, 1.0f);   // 环境光颜色（与 Forward 装配一致）
        ShaderID       m_shader   = ShaderID::invalid_id();  // deferred_lighting shader（懒加载）
        PipelineID     m_pipeline = PipelineID::invalid_id(); // 全屏三角形管线
    };
} // namespace ID
