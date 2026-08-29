#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Material/MaterialInstance.hpp"

namespace ID
{
    /**
     *  GBufferPass：延迟渲染几何阶段，把不透明批次以 MRT 形式写入 G-Buffer
     *  （ctx.gbuffer_fb：RT0 albedo+ambient / RT1 world_pos+spec / RT2 normal+shininess + 深度）。
     *  - 管线缓存：源管线（内嵌前向光照 shader）→ 同 layout/state 换 gbuffer shader 的管线
     *    （强制 blend=false / depth_write=true），key 为源 PipelineID 的 get_id()；
     *    ★ 假设源管线生命周期与进程同长（材质库管线不销毁），缓存不会悬空；
     *      若未来支持管线销毁，需要监听失效或改弱引用。
     */
    class ID_API GBufferPass : public RenderPass
    {
    public:
        GBufferPass();
        virtual ~GBufferPass() override = default;

    public:
        // 声明依赖：写 GBuffer（ctx.gbuffer_fb）
        void setup(RenderPassBuilder& builder) override;

        /**
         *  GBufferPass 的渲染阶段：
         *  - 绑定 ctx.gbuffer_fb + viewport + 清屏（color + depth，glClear 对全部 draw buffers 生效）
         *  - 遍历 ctx.opaque_batches：resolve_pipeline → 绑管线 → 应用材质参数 →
         *    u_mvp / u_model → draw_indexed
         */
        virtual void execute(RenderContext& ctx) override;

    private:
        void ensure_resources();

        // 源管线 → G-Buffer 管线（懒创建 + 缓存）
        PipelineID resolve_pipeline(const PipelineID src);

        // 以 gbuffer shader 为目标应用材质参数（父级默认 + 局部覆盖 + 纹理绑定）
        void apply_material(const MaterialInstance& material);

    private:
        ShaderID                   m_shader = ShaderID::invalid_id();   // gbuffer shader（懒加载）
        std::map<uint32_t, PipelineID> m_pipeline_cache;                // 源 PipelineID.get_id() → G-Buffer 管线
    };
} // namespace ID
