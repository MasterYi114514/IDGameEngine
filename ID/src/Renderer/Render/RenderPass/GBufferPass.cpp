#include "Renderer/Render/RenderPass/GBufferPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Render/RenderContext.hpp"

#include "Log/Log.hpp"

namespace ID
{
    GBufferPass::GBufferPass() : RenderPass("GBufferPass") { }

    void GBufferPass::setup(RenderPassBuilder& builder)
    {
        builder.writes(RGResource::GBuffer);   // 输出 ctx.gbuffer_fb（MRT：3 颜色附件 + 深度）
    }

    void GBufferPass::ensure_resources()
    {
        if (m_shader.is_valid())
        {
            return;
        }

        std::string vs = ShaderSourceLoader::load_shader_source("../Assets/shader/gbuffer.vsl");
        std::string fs = ShaderSourceLoader::load_shader_source("../Assets/shader/gbuffer.fsl");
        m_shader = ::ShaderManager::create(ShaderCreateInfo(vs, fs));
        if (!m_shader.is_valid())
        {
            ID_ERROR("[GBufferPass] gbuffer shader 加载失败（gbuffer.vsl / gbuffer.fsl）");
        }
    }

    PipelineID GBufferPass::resolve_pipeline(const PipelineID src)
    {
        const uint32_t key = src.get_id();
        auto it = m_pipeline_cache.find(key);
        if (it != m_pipeline_cache.end())
        {
            return it->second;
        }

        // 同 layout / 同 state 换 gbuffer shader；强制覆写 blend=false（G-Buffer 阶段禁止混合）、depth_write=true
        PipelineState state = IDRCmd::get_pipeline_state(src);
        state.blend       = false;
        state.depth_write = true;

        PipelineID pipeline = PipelineManager::create(
            PipelineCreateInfo(m_shader, IDRCmd::get_pipeline_layout(src), state));
        m_pipeline_cache[key] = pipeline;
        return pipeline;
    }

    /*
    *   apply_material：以 gbuffer shader 为目标应用材质参数（父级默认 + 局部覆盖 + 纹理绑定），
    *   合并逻辑与 MaterialInstance::apply() 一致。不能直接调用 material.apply()：
    *   它内部以材质自身 shader 查询 uniform location，而 set_param 的 glUniform 作用于
    *   当前绑定的 program，材质 shader 与 gbuffer shader 是两个不同 program，
    *   location 分配不一致会导致 uniform 写入错位（详见变更记录 Step 5）。
    */
    void GBufferPass::apply_material(const MaterialInstance& material)
    {
        if (!material.is_valid())
        {
            return;
        }

        const Material* parent = material.get_parent();

        // 1. 父级默认值
        for (const auto& [name, param] : parent->get_param_defaults())
        {
            Material::apply_param(m_shader, name, param);
        }

        for (const auto& [name, binding] : parent->get_texture_defaults())
        {
            IDRCmd::set_param(m_shader, name, static_cast<int>(binding.slot));
            if (binding.texture.is_valid())
            {
                IDRCmd::bind_texture(binding.texture, binding.slot);
            }
        }

        // 2. 局部覆盖
        for (const auto& [name, param] : material.get_param_overrides())
        {
            Material::apply_param(m_shader, name, param);
        }

        for (const auto& [name, binding] : material.get_texture_overrides())
        {
            IDRCmd::set_param(m_shader, name, static_cast<int>(binding.slot));
            if (binding.texture.is_valid())
            {
                IDRCmd::bind_texture(binding.texture, binding.slot);
            }
        }
    }

    void GBufferPass::execute(RenderContext& ctx)
    {
        ensure_resources();
        if (!m_shader.is_valid())
        {
            return;
        }

        // 绑定 G-Buffer FBO（无效时警告并跳过：延迟装配下 G-Buffer 缺失属装配错误）
        if (!ctx.gbuffer_fb.is_valid())
        {
            ID_WARN("[GBufferPass] ctx.gbuffer_fb 无效，跳过 G-Buffer 几何阶段");
            return;
        }
        IDRCmd::bind_framebuffer(ctx.gbuffer_fb);

        if (ctx.window_width != 0 && ctx.window_height != 0)
        {
            IDRCmd::set_viewport(0, 0, ctx.window_width, ctx.window_height);
        }

        // 清屏：glClear 对全部 draw buffers 生效（RT0/RT1/RT2 + 深度）
        IDRCmd::clear();

        // 不透明批次：写 G-Buffer（光照在 LightingPass 完成，此处不提交光源 uniform）
        for (const ModelSE& entry : ctx.opaque_batches)
        {
            const PipelineID pipeline = resolve_pipeline(entry.pipeline);

            // ★ 先绑定 gbuffer 管线：set_param 的 glUniform 作用于当前绑定的 program，
            //   必须确保当前 program = gbuffer program，材质参数 / u_mvp / u_model 才写入正确目标
            IDRCmd::bind_pipeline(pipeline);

            // 每批绘制前解绑纹理单元 0（与 RenderPass::draw_batch 一致：无纹理材质避免采样残留）
            IDRCmd::unbind_texture(0);

            const MaterialInstance& material = entry.material ? *entry.material : MaterialInstance(nullptr);
            apply_material(material);

            // 物体级 uniform（以 gbuffer pipeline 为目标，location 来自 gbuffer shader）
            const Mat4& model = entry.world_transform;
            Mat4 mvp = ctx.camera.get_projection_matrix() * ctx.camera.get_view_matrix() * model;
            IDRCmd::set_param(pipeline, "u_mvp", mvp);
            IDRCmd::set_param(pipeline, "u_model", model);

            auto& mesh = MeshFactory::get_mesh(entry.mesh);
            IDRCmd::draw_indexed(pipeline, mesh.get_vb(), mesh.get_ib());

            ID_RS_INC_DRAW_CALLS(ctx);
            ID_RS_INC_TRIANGLES(ctx, mesh.get_index_count() / 3);
        }
    }
} // namespace ID
