#include "Renderer/Render/RenderPass/ShadowPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"

#include "Log/Log.hpp"

#include <cmath>

namespace ID
{
    ShadowPass::ShadowPass()
        : RenderPass("ShadowPass")
    { }

    // ── 辅助：获取 ShadowMap 的 FBO ──
    FrameBufferID ShadowPass::get_shadow_fb() const
    {
        ShadowMap* map = ShadowManager::get_shadow_map(m_shadow_map_id);
        if(map == nullptr)
        {
            ID_WARN("ShadowPass::get_shadow_fb: 从 ShadowManager 获取 ShadowMap 失败");
            return FrameBufferID::invalid_id();
        }
        return map->get_fb();
    }

    // ── 找主方向光 ──
    bool ShadowPass::find_directional_light(const RenderContext& ctx,
                                             Light& out_light) const
    {
        for (const LightSE& entry : ctx.lights)
        {
            if (entry->type == LightType::Directional && entry->enabled)
            {
                out_light = *entry;
                return true;
            }
        }
        return false;
    }

    // ── 懒创建资源 ──
    void ShadowPass::ensure_resources(uint32_t map_size)
    {
        // 深度 shader
        if (!m_depth_shader.is_valid())
        {
            std::string vs = ShaderSourceLoader::load_shader_source(
                "../Assets/shader/depth.vsl");
            std::string fs = ShaderSourceLoader::load_shader_source(
                "../Assets/shader/depth.fsl");
            m_depth_shader = ::ShaderManager::create(ShaderCreateInfo(vs, fs));
            if (!m_depth_shader.is_valid())
            {
                ID_ERROR("[ShadowPass] 深度 shader 加载失败");
            }
        }

        // ShadowMap（通过 ShadowManager 池管理，分辨率不变时不重建）
        ShadowMap* map = m_shadow_map_id.is_valid() ? 
            ShadowManager::get_shadow_map(m_shadow_map_id) : nullptr;
        if (!map || !map->is_valid())
        {
            if (m_shadow_map_id.is_valid())
            {
                ShadowManager::destroy_shadow_map(m_shadow_map_id);
            }
            m_shadow_map_id = ShadowManager::create();

            map = ShadowManager::get_shadow_map(m_shadow_map_id);
            if (map)
            {
                map->set_fb(FBManager::create(FrameBufferCreateInfo(map_size, map_size)));
                map->set_type(ShadowMapType::Texture2D);
                ID_TRACE("[ShadowPass] 阴影 FBO 创建 {}x{}", map_size, map_size);
            }
        }
    }

    // ── Layout 哈希 ──
    uint64_t ShadowPass::hash_layout(const VertexBufferLayout& layout)
    {
        ID_TRACE("[ShadowPass] 计算 Layout 哈希");
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };

        mix(layout.get_stride());
        for (size_t i = 0; i < layout.get_attribute_count(); ++i)
        {
            const VertexBufferAttribute& attr = layout[i];
            mix(static_cast<uint64_t>(attr.type));
            mix(attr.offset);
            mix(attr.normalized ? 1u : 0u);
        }
        return h;
    }

    // ── 深度 Pipeline（按 layout 缓存）──
    PipelineID ShadowPass::get_depth_pipeline(const VertexBufferLayout& layout)
    {
        uint64_t key = hash_layout(layout);

        auto it = m_pipeline_cache.find(key);
        if (it != m_pipeline_cache.end())
            return it->second;

        PipelineState state;
        state.depth_test  = true;
        state.depth_write = true;
        state.cull_mode   = CullMode::Back;     // 背面剔除：只渲染朝向光源的正面
        state.blend       = false;

        PipelineID pipeline = PipelineManager::create(
            PipelineCreateInfo(m_depth_shader, layout, state));
        m_pipeline_cache[key] = pipeline;
        return pipeline;
    }

    // ── 渲染单个深度批次 ──
    void ShadowPass::draw_depth_batch(RenderContext& ctx, const ModelSE& entry)
    {
        const Mesh& mesh = MeshFactory::get_mesh(entry.mesh);
        PipelineID pipeline = get_depth_pipeline(mesh.get_layout());
        if (!pipeline.is_valid())
            return;

        IDRCmd::set_param(pipeline, "u_light_view_proj", m_light_view_proj);
        IDRCmd::set_param(pipeline, "u_model", entry.world_transform);
        IDRCmd::set_param(pipeline, "u_normal_bias", m_shadow_camera.get_config().param.normal_bias);
        IDRCmd::draw_indexed(pipeline, mesh.get_vb(), mesh.get_ib());

        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void ShadowPass::execute(RenderContext& ctx)
    {
        // 复位
        ctx.shadow_enabled  = false;
        ctx.shadow_fb       = FrameBufferID::invalid_id();
        ctx.light_view_proj = Math::get_identity_mat4();
        m_light_view_proj   = Math::get_identity_mat4();

        if (!m_enabled) return;

        // 找主方向光
        Light dir_light;
        if (!find_directional_light(ctx, dir_light))
            return;

        // 同步场景光源方向到 ShadowCamera（否则一直用默认的 (0,-1,0)）
        m_shadow_camera.set_direction(dir_light.drop.direction);

        // 读取阴影配置
        auto& cfg = m_shadow_camera.get_config();
        uint32_t map_size = shadow_quality_to_map_size(cfg.param.quality);

        // 懒创建资源
        ensure_resources(map_size);
        if (!m_depth_shader.is_valid())
        {
            ID_WARN("[ShadowPass] depth_shader 无效，无法渲染阴影");
            return;
        }

        ShadowMap* map = ShadowManager::get_shadow_map(m_shadow_map_id);
        if (!map || !map->is_valid())
        {
            ID_WARN("[ShadowPass] ShadowMap 无效，无法渲染阴影");
            return;
        }

        // 用 ShadowCamera 计算光源 VP 矩阵
        ShadowView sv = m_shadow_camera.compute_view(0, ctx.camera);
        m_light_view_proj = sv.view_proj;

        // 绑定阴影 FBO，清深度
        IDRCmd::bind_framebuffer(map->get_fb());
        IDRCmd::set_viewport(0, 0, map_size, map_size);
        IDRCmd::clear(false, true);

        // 渲染不透明物体到深度贴图
        for (const ModelSE& entry : ctx.opaque_batches)
        {
            draw_depth_batch(ctx, entry);
        }

        // 写回 ctx
        ctx.shadow_fb         = map->get_fb();
        ctx.light_view_proj   = m_light_view_proj;
        ctx.shadow_bias       = cfg.param.bias;
        ctx.shadow_pcf_radius = static_cast<int>(shadow_quality_to_pcf_kernel_size(cfg.param.quality));
        ctx.shadow_enabled    = true;
    }
} // namespace ID