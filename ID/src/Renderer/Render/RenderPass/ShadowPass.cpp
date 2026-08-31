#include "Renderer/Render/RenderPass/ShadowPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RendererSettings.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Core/IDArray.hpp"   // Array<float,16>：std140 mat4 同布局固定数组（ShadowBlockGPU）

#include "Log/Log.hpp"

#include <cmath>
#include <cstddef>   // offsetof（ShadowBlockGPU static_assert）

namespace ID
{
    // ShadowBlockGPU — 与 shader 端 ShadowBlock（std140, binding 0）一一对应，勿改顺序
    // 矩阵用 Array<float,16> 存储
    // 布局：mat4[4] = 256B（0~255）+ vec4 × 4 = 64B（256~319），共 320B
    struct ShadowBlockGPU
    {
        Array<float, 16> light_space_mvps[MAX_CASCADES];   // 各层光源 VP（列优先，64B × 4）
        Vec4  cascade_splits;                       // xyzw = 层 0~3 远边界（视空间距离，正数）
        Vec4  cascade_bias_scales;                  // xyzw = 层 0~3（texel 尺寸维度）
        Vec4  shadow_params;                        // x = count, y = pcf_radius, z = filter, w = light_size
        Vec4  misc;                                 // x = bias, y = light_index, z = enabled, w = pad
    };
    static_assert(sizeof(Vec4) == 16, "Vec4 必须为 16B（glm::vec4），std140 vec4 对齐前提");
    static_assert(offsetof(ShadowBlockGPU, cascade_splits)      == 256, "ShadowBlockGPU 布局不符 std140（mat4[4] 占 256B）");
    static_assert(offsetof(ShadowBlockGPU, cascade_bias_scales) == 272, "ShadowBlockGPU 布局不符 std140（vec4 16B 对齐）");
    static_assert(offsetof(ShadowBlockGPU, shadow_params)       == 288, "ShadowBlockGPU 布局不符 std140");
    static_assert(offsetof(ShadowBlockGPU, misc)                == 304, "ShadowBlockGPU 布局不符 std140");
    static_assert(sizeof(ShadowBlockGPU)                        == 320, "ShadowBlockGPU 布局不符 std140（共 320B）");

    ShadowPass::ShadowPass()
        : RenderPass("ShadowPass")
    { }

    void ShadowPass::setup(RenderPassBuilder& builder)
    {
        builder.writes(RGResourceName::ShadowMap);
    }

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

        // ShadowMap（通过 ShadowManager 池管理，分辨率/层数不变时不重建）
        ShadowMap* map = m_shadow_map_id.is_valid() ? 
            ShadowManager::get_shadow_map(m_shadow_map_id) : nullptr;
        if (!map || !map->is_valid()
            || map->get_size() != map_size
            || map->get_layer_count() != m_shadow_layer_count)
        {
            if (m_shadow_map_id.is_valid())
            {
                // 销毁顺序：ShadowMap::destroy 内部先 FBO 后纹理
                ShadowManager::destroy_shadow_map(m_shadow_map_id);
            }
            m_shadow_map_id = ShadowManager::create(map_size, m_shadow_layer_count);

            map = ShadowManager::get_shadow_map(m_shadow_map_id);
            if (map)
            {
                ID_TRACE("[ShadowPass] 阴影 array FBO 创建 {}x{} x {} 层", map_size, map_size, m_shadow_layer_count);
            }
        }

        // Sampler Object（长命懒创建；sampler 状态优先于纹理对象参数，实现同纹理 raw/cmp 双采样状态）
        if (!m_sampler_raw.is_valid())
        {
            m_sampler_raw = SamplerManager::create(
                SamplerCreateInfo(TextureFilter::Nearest, TextureCompare::None, TextureWrap::ClampToBorder));
        }
        if (!m_sampler_cmp.is_valid())
        {
            m_sampler_cmp = SamplerManager::create(
                SamplerCreateInfo(TextureFilter::Linear, TextureCompare::RefToTexture, TextureWrap::ClampToBorder));
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
        ctx.shadow_enabled       = false;
        ctx.shadow_fb            = FrameBufferID::invalid_id();
        ctx.shadow_depth_array   = TextureID::invalid_id();
        ctx.shadow_sampler_raw   = SamplerID::invalid_id();
        ctx.shadow_sampler_cmp   = SamplerID::invalid_id();
        ctx.shadow_ubo           = UniformBufferID::invalid_id();
        ctx.light_view_proj      = Math::get_identity_mat4();
        m_light_view_proj        = Math::get_identity_mat4();

        // 级联数组复位（逐元素；Mat4 默认构造不保证初始化）
        ctx.cascade_count = 1;
        for (uint32_t i = 0; i < MAX_CASCADES; ++i)
        {
            ctx.light_view_projs[i] = Math::get_identity_mat4();
            ctx.cascade_splits[i]   = 0.0f;
            ctx.cascade_bias_scales[i] = 1.0f;   // texel 尺寸维度 bias 缩放（Step 8.5）
        }

        // 阴影 UBO 懒创建（binding 0 = ShadowBlock；长命持有，跨帧复用）
        if (!m_shadow_ubo.is_valid())
        {
            m_shadow_ubo = UBManager::create(
                UniformBufferCreateInfo(sizeof(ShadowBlockGPU), 0, BufferUsageHint::DynamicDraw));
        }

        if (!m_enabled)
        {
            upload_shadow_block(ctx, false);   // 无阴影帧也上传 enabled=0，防 UBO 残留上帧数据
            return;
        }

        // 找主方向光
        Light dir_light;
        int dir_light_index = -1;
        for (int i = 0; i < static_cast<int>(ctx.lights.size()); ++i)
        {
            if (ctx.lights[i]->type == LightType::Directional && ctx.lights[i]->enabled)
            {
                dir_light = *ctx.lights[i];
                dir_light_index = i;
                break;
            }
        }
        if (dir_light_index < 0)
        {
            upload_shadow_block(ctx, false);   // 同上：无阴影帧防残留
            return;
        }

        // 同步场景光源方向到 ShadowCamera（否则一直用默认的 (0,-1,0)）
        m_shadow_camera.set_direction(dir_light.drop.direction);

        // 读取阴影配置
        auto& cfg = m_shadow_camera.get_config();
        uint32_t map_size = shadow_quality_to_map_size(cfg.param.quality);

        // 级联层数同步 RendererSettings（唯一事实来源）
        m_shadow_layer_count = std::clamp(get_renderer_settings().cascade_count, 1u, MAX_CASCADES);

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

        // 逐层渲染：attach → clear → draw（glClear 只清当前 attach 的层）
        const uint32_t count = m_shadow_layer_count;
        for (uint32_t layer = 0; layer < count; ++layer)
        {
            ShadowView sv = m_shadow_camera.compute_view(layer, ctx.camera);

            IDRCmd::bind_framebuffer(map->get_fb());
            IDRCmd::attach_framebuffer_depth_layer(map->get_fb(), map->get_depth_array(), layer);
            IDRCmd::set_viewport(0, 0, map_size, map_size);
            IDRCmd::clear(false, true);

            // draw_depth_batch 内 set_param u_light_view_proj = m_light_view_proj
            m_light_view_proj = sv.view_proj;
            // normal_bias 逐层缩放：远层 texel 世界尺寸更大，法线偏移量需等比放大（depth.vsl 无改动）
            m_layer_bias_scale = sv.bias_scale;
            for (const ModelSE& entry : ctx.opaque_batches)
            {
                draw_depth_batch(ctx, entry);
            }

            ctx.light_view_projs[layer] = sv.view_proj;
            ctx.cascade_splits[layer]   = sv.far_bound;   // 视空间距离（正数）；语义与 shader 端注释互引
            ctx.cascade_bias_scales[layer] = sv.bias_scale;   // texel 尺寸维度 bias 缩放（Step 8.5）
        }
        ctx.cascade_count = count;

        // 写回 ctx
        ctx.shadow_fb          = map->get_fb();
        ctx.shadow_depth_array = map->get_depth_array();
        ctx.shadow_sampler_raw = m_sampler_raw;
        ctx.shadow_sampler_cmp = m_sampler_cmp;
        ctx.shadow_ubo         = m_shadow_ubo;   // 消费点 bind binding 0（ShadowBlock）
        ctx.light_view_proj    = ctx.light_view_projs[0];   // 旧单级联字段 = 层 0 值（P8 前过渡期可用）
        ctx.shadow_bias        = cfg.param.bias;
        ctx.shadow_pcf_radius  = static_cast<int>(shadow_quality_to_pcf_kernel_size(cfg.param.quality));
        ctx.shadow_enabled     = true;
        ctx.shadow_light_index = dir_light_index;

        // 上传完整阴影块（一次 glBufferSubData，替代消费点 10+ 次逐元素 set_param）
        upload_shadow_block(ctx, true);
    }

    // 填充并上传 ShadowBlock（std140；消费点只需 bind binding 0）
    void ShadowPass::upload_shadow_block(RenderContext& ctx, bool enabled)
    {
        if (!m_shadow_ubo.is_valid()) return;

        ShadowBlockGPU block{};
        const RendererSettings& settings = get_renderer_settings();

        if (enabled)
        {
            for (uint32_t i = 0; i < MAX_CASCADES; ++i)
            {
                // Array<float,16>（IDArray.hpp）：std140 mat4 同布局固定数组；构造经 memcpy 拷贝列优先数据
                block.light_space_mvps[i] = Array<float, 16>(ctx.light_view_projs[i].get_data());
            }
            // 各层远边界（视空间距离，正数；shader 端 pick_cascade 选层用）
            block.cascade_splits = Vec4(ctx.cascade_splits[0], ctx.cascade_splits[1],
                ctx.cascade_splits[2], ctx.cascade_splits[3]);
            // texel 尺寸维度 bias 缩放（Step 8.5；远层 texel 世界尺寸更大）
            block.cascade_bias_scales = Vec4(ctx.cascade_bias_scales[0], ctx.cascade_bias_scales[1],
                ctx.cascade_bias_scales[2], ctx.cascade_bias_scales[3]);
            block.shadow_params = Vec4(static_cast<float>(ctx.cascade_count),
                static_cast<float>(ctx.shadow_pcf_radius),
                static_cast<float>(settings.shadow_filter),
                settings.light_size);
            block.misc = Vec4(ctx.shadow_bias,
                static_cast<float>(ctx.shadow_light_index),
                1.0f, 0.0f);
        }
        else
        {
            // 无阴影帧：仅 enabled = 0（矩阵字段保持默认构造值，shader 在 enabled=0 时不消费）
            block.misc = Vec4(0.0f, -1.0f, 0.0f, 0.0f);
        }

        IDRCmd::update_uniform_buffer(m_shadow_ubo, &block, sizeof(block));
    }
} // namespace ID