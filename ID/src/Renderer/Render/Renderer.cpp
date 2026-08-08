#include "Renderer/Render/Renderer.hpp"
#include "Log/Log.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderPass/ForwardPass.hpp"
#include "Renderer/Render/RenderPass/ShadowPass.hpp"
#include "Renderer/Render/RenderPass/SkyboxPass.hpp"
#include "Renderer/Render/RenderPass/TransparentPass.hpp"
#include "Renderer/Render/RenderPass/PostProcessPass.hpp"
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
            // ★ 依赖 IDRenderer 增强：FrameBufferCreateInfo.color_format
            ID::FrameBufferCreateInfo info(w, h);
            info.color_format = ID::TextureFormat::RGBA16F;
            fb_state.fb = FBManager::create(info);
            fb_state.width    = w;
            fb_state.height   = h;
            ID_INFO("Renderer: 场景 HDR FBO 重建 {}x{} (RGBA16F)", w, h);
        }
        return fb_state.fb;
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

        // 如果 scene 非空，自动收集场景内的 MeshRendererComponent / LightComponent
        if(scene != nullptr)
        {
            for(GameObject::ID id : scene->find_game_objects_with_component<MeshRendererComponent>())
            {
                GameObject& go = scene->get_game_object(id);
                if (!go.is_active())
                {
                    continue;
                }
                MeshRendererComponent* mrc = go.get_component<MeshRendererComponent>();
                if (mrc != nullptr)
                {
                    submit(mrc->get_model(), go.get_world_matrix());
                }
            }

            for(GameObject::ID id : scene->find_game_objects_with_component<LightComponent>())
            {
                GameObject& go = scene->get_game_object(id);
                if (!go.is_active())
                {
                    continue;
                }
                LightComponent* lc = go.get_component<LightComponent>();
                if (lc != nullptr && lc->get_light().enabled)
                {
                    submit(lc->get_light());
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
                scene_fb
            };
            get_render_graph().execute(ctx);
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
        RenderGraph& graph = get_render_graph();
        graph.clear();

        if (shadow)
        {
            graph.add_pass<ShadowPass>();
        }

        // ForwardPass 输出到 ctx.scene_fb（Renderer::render 注入）；有天空盒时透明拆分
        ForwardPass& forward = graph.add_pass<ForwardPass>();
        forward.set_use_scene_fb(true);                 // 渲染到场景 HDR FBO
        forward.set_render_transparent(!skybox);        // 有天空盒时透明拆分到 TransparentPass

        if (skybox)
        {
            graph.add_pass<SkyboxPass>();
            graph.add_pass<TransparentPass>();
        }

        if (post_process)
        {
            graph.add_pass<PostProcessPass>();      // 输出默认屏幕
        }

        ID_INFO("Renderer: 视觉管线装配完成 (shadow={} skybox={} post_process={})",
            shadow, skybox, post_process);
    }
} // namespace ID::Renderer