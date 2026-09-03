#include "Renderer/Render/Renderer.hpp"
#include "Renderer/IDRCore.hpp"
#include "Log/Log.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderPass/ForwardPass.hpp"
#include "Renderer/Render/RenderPass/GBufferPass.hpp"
#include "Renderer/Render/RenderPass/LightingPass.hpp"
#include "Renderer/Render/RenderPass/ShadowPass.hpp"
#include "Renderer/Render/RenderPass/SkyboxPass.hpp"
#include "Renderer/Render/RenderPass/TransparentPass.hpp"
#include "Renderer/Render/RenderPass/PostProcessPass.hpp"
#include "Renderer/Render/FullscreenQuad.hpp"
#include "Renderer/Render/RendererSettings.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Scene/Component/LightComponent.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"


namespace
{
    struct PipelineKey
    {
        ID::ShaderID    shader;
        uint64_t        layout_hash;
        bool            transparent;

        bool operator==(const PipelineKey&) const = default;
    };

    struct PipelineKeyHash
    {
        size_t operator()(const PipelineKey& key) const
        {
            size_t h = 1469598103934665603ull;      // FNV-1a
            auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
            mix(key.shader.get_id());
            mix(key.layout_hash);
            mix(key.transparent ? 1 : 0);
            return static_cast<size_t>(h);
        }
    };

    // pipeline 缓存（跨帧保留，材质/网格组合第一次使用时创建）
    std::unordered_map<PipelineKey, ID::PipelineID, PipelineKeyHash> g_pipeline_cache;

    size_t hash_layout(const ID::VertexBufferLayout& layout)
    {
        size_t h = 1469598103934665603ull;   // FNV-1a
        auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };

        mix(layout.get_stride());
        for (size_t i = 0; i < layout.get_attribute_count(); ++i)
        {
            const ID::VertexBufferAttribute& attr = layout[i];
            mix(static_cast<uint64_t>(attr.type));
            mix(attr.offset);
            mix(attr.normalized ? 1 : 0);
        }
        return h;
    }

    /**
     *  从 pipeline cache 中获取对应的 pipeline，如果不存在则创建新的 pipeline 并缓存
     */
    ID::PipelineID get_pipeline(const ID::Material& material, const ID::VertexBufferLayout& layout)
    {
        PipelineKey key{ material.get_shader(), hash_layout(layout), material.is_transparent() };

        auto it = g_pipeline_cache.find(key);
        if (it != g_pipeline_cache.end())
        {
            return it->second;
        }

        // 透明材质：开混合 + 关闭深度写入；其余用默认管线状态
        ID::PipelineState state;
        if (material.is_transparent())
        {
            state.blend       = true;
            state.blend_src   = ID::BlendFactor::SrcAlpha;
            state.blend_dst   = ID::BlendFactor::OneMinusSrcAlpha;
            state.depth_write = false;
        }

        ID::PipelineCreateInfo info(material.get_shader(), layout, state);
        ID::PipelineID pipeline = PipelineManager::create(info);
        g_pipeline_cache[key] = pipeline;
        return pipeline;
    }

    // 懒加载帧队列
    std::vector<ID::ModelSE>& opaque_batches()          // 不透明批次
    {
        static std::vector<ID::ModelSE> s_opaque;
        return s_opaque;
    }

    std::vector<ID::ModelSE>& transparent_batches()     // 透明批次
    {
        static std::vector<ID::ModelSE> s_transparent;
        return s_transparent;
    }

    std::vector<ID::LightSE>& lights()                        // 光源列表
    {
        static std::vector<ID::LightSE> s_lights;
        return s_lights;
    }

    ID::RendererStatistics& statistics()                    // 统计信息
    {
        static ID::RendererStatistics s_statistics;
        return s_statistics;
    }

    struct SceneFBState
    {
        ID::FrameBufferID fb = ID::FrameBufferID::invalid_id();
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    SceneFBState& scene_fb_state()
    {
        static SceneFBState s_scene_fb = SceneFBState();
        return s_scene_fb;
    }

    ID::FrameBufferID ensure_scene_fb(uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0)
        {
            return ID::FrameBufferID::invalid_id();
        }

        auto& fb_state = scene_fb_state();
        if (!fb_state.fb.is_valid() || fb_state.width != w || fb_state.height != h)
        {
            if (fb_state.fb.is_valid())
            {
                FBManager::destroy(fb_state.fb);
            }
            // HDR 场景渲染目标：RGBA16F 颜色附件 + 深度附件
            ID::FrameBufferCreateInfo info(w, h, ID::TextureFormat::RGBA16F);
            fb_state.fb = FBManager::create(info);
            fb_state.width    = w;
            fb_state.height   = h;
            ID_INFO("Renderer: 场景 HDR FBO 重建 {}x{} (RGBA16F)", w, h);
        }
        return fb_state.fb;
    }

    struct ViewportFBState
    {
        ID::FrameBufferID fb = ID::FrameBufferID::invalid_id();
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    ViewportFBState& viewport_fb_state()
    {
        static ViewportFBState s_viewport_fb = ViewportFBState();
        return s_viewport_fb;
    }

    /*
    *   ensure_viewport_fb 确保"最终显示 FBO"存在且尺寸匹配。
    *   PostProcessPass 输出到该 FBO，随后 blit 到默认 framebuffer 供窗口显示，
    *   同时其颜色纹理可供 ImGui Viewport 面板采样显示。
    */
    ID::FrameBufferID ensure_viewport_fb(uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0)
        {
            return ID::FrameBufferID::invalid_id();
        }

        auto& fb_state = viewport_fb_state();
        if (!fb_state.fb.is_valid() || fb_state.width != w || fb_state.height != h)
        {
            if (fb_state.fb.is_valid())
            {
                FBManager::destroy(fb_state.fb);
            }
            // 最终显示目标：RGBA8（tone mapping / gamma 已在后处理完成）
            ID::FrameBufferCreateInfo info(w, h, ID::TextureFormat::RGBA8);
            info.has_depth_attachment = false;
            fb_state.fb = FBManager::create(info);
            fb_state.width    = w;
            fb_state.height   = h;
            ID_INFO("Renderer: 显示 FBO 重建 {}x{} (RGBA8)", w, h);
        }
        return fb_state.fb;
    }

    // 当前 visual pipeline 是否包含后处理（无后处理时需手动拷贝 scene_fb → viewport_fb）
    bool& post_process_enabled_flag()
    {
        static bool s_enabled = true;
        return s_enabled;
    }

    // ── 回退呈现管线（无后处理装配时：scene_fb → tone map + gamma → viewport_fb）──
    // 复用 postprocess.vsl/fsl（u_mode=3 composite，无 bloom），替代裸 blit：
    // HDR→LDR 转换不丢失；tone map / gamma 效果位与 PostProcessPass 同源读 RendererSettings。
    // 进程级懒创建，随进程生命周期存活（与 ensure_*_fb 模式一致）。
    ID::ShaderID& present_shader()
    {
        static ID::ShaderID s_shader = ID::ShaderID::invalid_id();
        if (!s_shader.is_valid())
        {
            std::string vs = ID::ShaderSourceLoader::load_shader_source("../Assets/shader/postprocess.vsl");
            std::string fs = ID::ShaderSourceLoader::load_shader_source("../Assets/shader/postprocess.fsl");
            s_shader = ShaderManager::create(ID::ShaderCreateInfo(vs, fs));
            if (!s_shader.is_valid())
            {
                ID_ERROR("Renderer: 回退呈现 shader（postprocess.vsl / .fsl）加载失败");
            }
        }
        return s_shader;
    }

    ID::PipelineID& present_pipeline()
    {
        static ID::PipelineID s_pipeline = ID::PipelineID::invalid_id();
        if (!s_pipeline.is_valid())
        {
            // 全屏呈现：关深度测试/写入/混合（覆盖整个屏幕）
            ID::PipelineState state;
            state.depth_test  = false;
            state.depth_write = false;
            state.cull_mode   = ID::CullMode::None;
            state.blend       = false;
            s_pipeline = PipelineManager::create(
                ID::PipelineCreateInfo(present_shader(), ID::FullscreenQuad::layout(), state));
        }
        return s_pipeline;
    }

    /*
    *   disabled_shadow_ubo：无 ShadowPass 装配帧的禁用阴影 UBO（320B 全零，misc.z = enabled = 0）。
    *   ShadowPass 不在管线时无人上传 enabled=0 块，binding 0 残留旧帧 enabled=1 数据
    *   （旧 MVP + enabled=1）→ 关闭阴影后阴影残留。此 UBO 交给消费点 bind，保证跳过阴影。
    */
    ID::UniformBufferID& disabled_shadow_ubo()
    {
        static ID::UniformBufferID s_ubo = ID::UniformBufferID::invalid_id();
        if (!s_ubo.is_valid())
        {
            // ShadowBlockGPU 布局（std140，与 shader ShadowBlock 一致）：共 320B；全零即 enabled=0
            s_ubo = UBManager::create(
                ID::UniformBufferCreateInfo(320, 0, ID::BufferUsageHint::DynamicDraw));
            const unsigned char zero_block[320] = {};
            IDRCmd::update_uniform_buffer(s_ubo, zero_block, sizeof(zero_block));
        }
        return s_ubo;
    }

    struct GBufferFBState
    {
        ID::FrameBufferID fb = ID::FrameBufferID::invalid_id();
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    GBufferFBState& gbuffer_fb_state()
    {
        static GBufferFBState s_gbuffer_fb = GBufferFBState();
        return s_gbuffer_fb;
    }

    /*
    *   ensure_gbuffer_fb 确保延迟渲染的 G-Buffer FBO 存在且尺寸匹配。
    *   3 个颜色附件：RT0 RGBA8（albedo.rgb + ambient_strength.a）、
    *   RT1 RGBA16F（world_pos.rgb + spec_strength.a）、RT2 RGBA16F（normal.rgb + shininess.a），
    *   外加深度附件（DEPTH24，与场景 FBO 一致可 blit）。
    */
    ID::FrameBufferID ensure_gbuffer_fb(uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0)
        {
            return ID::FrameBufferID::invalid_id();
        }

        auto& fb_state = gbuffer_fb_state();
        if (!fb_state.fb.is_valid() || fb_state.width != w || fb_state.height != h)
        {
            if (fb_state.fb.is_valid())
            {
                FBManager::destroy(fb_state.fb);
            }
            // 延迟路径固定 samples = 1（MSAA 延迟解析本期不做）
            ID::FrameBufferCreateInfo info(w, h,
                std::vector<ID::TextureFormat>{ ID::TextureFormat::RGBA8, ID::TextureFormat::RGBA16F, ID::TextureFormat::RGBA16F });
            fb_state.fb = FBManager::create(info);
            fb_state.width  = w;
            fb_state.height = h;
            ID_INFO("Renderer: G-Buffer FBO 重建 {}x{} (RGBA8 + RGBA16F x2)", w, h);
        }
        return fb_state.fb;
    }

    // 当前渲染路径（Forward / Deferred）
    ID::Renderer::RenderPath& render_path_state()
    {
        static ID::Renderer::RenderPath s_path = ID::Renderer::RenderPath::Forward;
        return s_path;
    }

    // 当前 visual pipeline 三开关状态（set_render_path 重装配时复用）
    struct PipelineFlags
    {
        bool shadow       = true;
        bool skybox       = false;
        bool post_process = true;
    };

    PipelineFlags& pipeline_flags_state()
    {
        static PipelineFlags s_flags;
        return s_flags;
    }

    /*
    *   rebuild_pipeline：按当前渲染路径装配视觉管线（Forward / Deferred 分支）
    *   set_visual_pipeline 与 set_render_path 共用此入口，避免两处复制装配代码
    */
    void rebuild_pipeline(bool shadow, bool skybox, bool post_process)
    {
        ID::RenderGraph& graph = ID::Renderer::get_render_graph();
        graph.clear();

        if (shadow)
        {
            graph.add_pass<ID::ShadowPass>();
        }

        if (render_path_state() == ID::Renderer::RenderPath::Deferred)
        {
            // Deferred 分支：几何（G-Buffer）+ 光照（全屏）两阶段必装
            graph.add_pass<ID::GBufferPass>();                                      // 输出 ctx.gbuffer_fb
            graph.add_pass<ID::LightingPass>(ID::Vec3(1.0f, 1.0f, 1.0f));           // 输出 ctx.scene_fb（环境光暂用固定白色）
        }
        else
        {
            // Forward 分支（现有装配不动）：ForwardPass 输出到 ctx.scene_fb；有天空盒时透明拆分
            ID::ForwardPass& forward = graph.add_pass<ID::ForwardPass>();
            forward.set_use_scene_fb(true);                 // 渲染到场景 HDR FBO
            forward.set_render_transparent(!skybox);        // 有天空盒时透明拆分到 TransparentPass
        }

        if (skybox)
        {
            graph.add_pass<ID::SkyboxPass>();
            graph.add_pass<ID::TransparentPass>();
        }

        if (post_process)
        {
            graph.add_pass<ID::PostProcessPass>();      // 输出到 ctx.viewport_fb（显示 FBO）
        }

        graph.compile();   // 装配期立即验证 + 输出编译日志（执行序 / 剔除 / 悬空警告）

        ID_INFO("Renderer: 视觉管线装配完成 (path={} shadow={} skybox={} post_process={})",
            render_path_state() == ID::Renderer::RenderPath::Deferred ? "Deferred" : "Forward",
            shadow, skybox, post_process);
    }
} // 匿名命名空间

// 获取帧队列的宏
#define OBatches opaque_batches()
#define TBatches transparent_batches()
#define Lights   lights()
#define Stats    statistics()

namespace ID::Renderer
{
    void submit(const Model& model, const Mat4& world_transform)
    {
        if(!model.is_valid())
        {
            ID_WARN("Renderer::submit() 提交了无效的 Model");
            return;
        }

        PipelineID pipeline = get_pipeline(*(model.get_material().get_parent()), 
            model.get_mesh().get_layout());

        if(model.get_material().is_transparent())
        {
            // 透明批次
            TBatches.emplace_back(model, world_transform, pipeline);
        }
        else    // 不透明批次
        {
            OBatches.emplace_back(model, world_transform, pipeline);
        }
    }

    void submit(const Light& light)
    {
        if(!light.enabled)
        {
            ID_WARN("Renderer::submit() 提交了未启用的 Light");
            return;
        }
        Lights.emplace_back(light);
    }

    void render(const Camera& camera, Scene* scene, uint32_t window_width, 
        uint32_t window_height, float time)
    {
        reset_statistics();     // 清理统计信息，确保每帧统计独立

        // 如果 scene 非空，自动收集场景内的 MeshRendererComponent / LightComponent（按池遍历，下标循环只读）
        if(scene != nullptr)
        {
            auto& mesh_pool = scene->get_component_registry().pool<MeshRendererComponent>();
            for (size_t i = 0; i < mesh_pool.size(); ++i)
            {
                GameObject::ID go_id = mesh_pool.owners()[i];
                if (!scene->is_game_object_valid(go_id)) continue;    // 防御：正常不触发（GO 销毁时组件已出池）

                GameObject& go = scene->get_game_object(go_id);
                if (!go.is_active())
                {
                    continue;
                }
                MeshRendererComponent& mrc = mesh_pool.components()[i];

                // 未激活的 MeshRendererComponent 不参与渲染
                if (!mrc.is_active())
                {
                    continue;
                }

                auto transform_comp = go.get_component<TransformComponent>();
                if(transform_comp == nullptr)
                {
                    ID_WARN("GameObject '{}' (ID={}) 缺少 TransformComponent，无法获取世界矩阵，MeshRendererComponent 将被忽略",
                        go.get_name(), go.get_id());
                }
                else if(transform_comp->is_active())
                {
                    submit(mrc.get_model(), transform_comp->get_world_matrix());
                }
                // else：TransformComponent 未激活，跳过渲染（静默，属于正常状态）
            }

            auto& light_pool = scene->get_component_registry().pool<LightComponent>();
            for (size_t i = 0; i < light_pool.size(); ++i)
            {
                GameObject::ID go_id = light_pool.owners()[i];
                if (!scene->is_game_object_valid(go_id)) continue;

                GameObject& go = scene->get_game_object(go_id);
                if (!go.is_active())
                {
                    continue;
                }
                LightComponent& lc = light_pool.components()[i];
                // 未激活的 LightComponent 不参与渲染
                if (lc.is_active() && lc.get_light().enabled)
                {
                    submit(lc.get_light());
                }
            }

        }

        // ② 计算相机距离并排序（场景收集/手动提交 两种模式均需执行）
        //    不透明：pipeline 分组（减少状态切换）+ 近→远
        //    透明：远→近（back-to-front）
        {
            const Vec3 cam_pos = camera.get_pose().position;

            auto calc_distance = [&cam_pos](ModelSE& e)
            {
                Vec3 pos(e.world_transform[0][3],
                        e.world_transform[1][3],
                        e.world_transform[2][3]);
                Vec3 delta = pos - cam_pos;
                e.view_distance_sq = delta.dot(delta);
            };

            for (ModelSE& e : opaque_batches())      { calc_distance(e); }
            for (ModelSE& e : transparent_batches()) { calc_distance(e); }

            std::stable_sort(opaque_batches().begin(), opaque_batches().end(),
                [](const ModelSE& a, const ModelSE& b)
                {
                    if (a.pipeline != b.pipeline)
                    {
                        return a.pipeline.get_id() < b.pipeline.get_id();
                    }
                    return a.view_distance_sq < b.view_distance_sq;   // 近→远
                });

            std::stable_sort(transparent_batches().begin(), transparent_batches().end(),
                [](const ModelSE& a, const ModelSE& b)
                {
                    return a.view_distance_sq > b.view_distance_sq;   // 远→近
                });
        }

        {
            RendererStatistics& stats = statistics();
            stats.lights      = static_cast<uint32_t>(lights().size());
            stats.opaque      = static_cast<uint32_t>(opaque_batches().size());
            stats.transparent = static_cast<uint32_t>(transparent_batches().size());

            FrameBufferID scene_fb = ensure_scene_fb(window_width, window_height);
            FrameBufferID viewport_fb = ensure_viewport_fb(window_width, window_height);
            FrameBufferID gbuffer_fb = ensure_gbuffer_fb(window_width, window_height);

            RenderContext ctx
            {
                camera,
                window_width,
                window_height,
                time,
                opaque_batches(),
                transparent_batches(),
                lights(),
                &stats,
                gbuffer_fb,
                scene_fb,
                viewport_fb
            };

            // ShadowPass 未装配：注入禁用阴影 UBO（320B 全零，enabled=0），防 binding 0 残留
            // 旧帧 enabled=1 块（旧 MVP）→ 关闭阴影后阴影残留；消费点无条件 bind 此 UBO
            if(!pipeline_flags_state().shadow && !ctx.shadow_ubo.is_valid())
            {
                ctx.shadow_ubo = disabled_shadow_ubo();
            }

            get_render_graph().execute(ctx);

            // 无后处理时 PostProcessPass 不参与渲染：回退呈现管线做 tone map + gamma（替代裸 blit，
            // 保证 HDR→LDR 转换不丢失；tone map / gamma 效果位与 PostProcessPass 同源读 RendererSettings）
            if(!post_process_enabled_flag()
                && scene_fb.is_valid() && viewport_fb.is_valid())
            {
                IDRCmd::bind_framebuffer_color(scene_fb, 0, 0);    // HDR 场景 → slot 0
                IDRCmd::unbind_sampler(0);   // 解绑残留 sampler（回退纹理 ClampToEdge，同 PostProcessPass）
                IDRCmd::set_param(present_pipeline(), "u_input", 0);
                IDRCmd::set_param(present_pipeline(), "u_mode", 3);   // composite（无 bloom）
                IDRCmd::set_param(present_pipeline(), "u_has_bloom", 0);
                IDRCmd::unbind_texture(1);
                const RendererSettings& settings = get_renderer_settings();
                IDRCmd::set_param(present_pipeline(), "u_tone_mapping", settings.post_tone_mapping ? 1 : 0);
                IDRCmd::set_param(present_pipeline(), "u_gamma", settings.post_gamma ? 1 : 0);

                IDRCmd::bind_framebuffer(viewport_fb);
                IDRCmd::set_viewport(0, 0, window_width, window_height);
                IDRCmd::draw_arrays(present_pipeline(), FullscreenQuad::vertex_buffer());
                ID_RS_INC_DRAW_CALLS(ctx);
            }

            // 显示 FBO → 默认 framebuffer（窗口可见）
            if(viewport_fb.is_valid())
            {
                RenderCommand::blit_framebuffer_to_default(viewport_fb,
                    window_width, window_height);
            }
        }

        clear_submissions();
    }

    void clear_submissions()
    {
        OBatches.clear();
        TBatches.clear();
        Lights.clear();
    }

#ifdef _ID_DEBUG
    void reset_statistics()
    {
        Stats = RendererStatistics{};
    }
#else
    void reset_statistics() { }
#endif

    RenderGraph& get_render_graph()
    {
        static RenderGraph s_graph = []()
        {
            RenderGraph graph;
            graph.add_pass<ForwardPass>();
            return graph;
        }();
        return s_graph;
    }

    void reset_render_graph()
    {
        get_render_graph().clear();
        get_render_graph().add_pass<ForwardPass>();
    }

    const RendererStatistics& get_statistics()
    {
        return Stats;
    }

    void set_visual_pipeline(bool shadow, bool skybox, bool post_process)
    {
        auto& flags = pipeline_flags_state();
        flags.shadow       = shadow;
        flags.skybox       = skybox;
        flags.post_process = post_process;
        rebuild_pipeline(shadow, skybox, post_process);
    }

    void set_render_path(RenderPath path)
    {
        if (render_path_state() == path)
        {
            return;
        }

        render_path_state() = path;

        // 按当前三开关状态重装配（与 set_visual_pipeline 同一入口）
        auto& flags = pipeline_flags_state();
        rebuild_pipeline(flags.shadow, flags.skybox, flags.post_process);
    }

    RenderPath get_render_path()
    {
        return render_path_state();
    }

    FrameBufferID get_gbuffer_fb()
    {
        return gbuffer_fb_state().fb;
    }

    FrameBufferID get_viewport_fb()
    {
        return viewport_fb_state().fb;
    }
} // namespace ID::Renderer