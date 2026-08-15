#include "IDpch.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/AssetManager.hpp"

#include "Log/Log.hpp"
#include "IDJson.hpp"

#include "BasicPool.hpp"

#include <fstream>

namespace
{
    // 场景池以 SceneID 为键（名字只是名字，可重复/可修改，不承担身份）
    std::unordered_map<ID::SceneID, std::unique_ptr<ID::Scene>, ID::SceneID::Hash> g_ScenePool;
    ID::SceneID g_NextID{ 1 };      // default_scene 固定占用 0
    ID::Scene* g_CurrentScene = nullptr;
} // 匿名命名空间

namespace ID
{
    Scene SceneManager::default_scene{"Default Scene", SceneID{0}};

    Scene& SceneManager::create_scene(const std::string& name)
    {
        // 每次创建分配新的 SceneID，同名也互不覆盖（名字不承担身份）
        SceneID new_id = g_NextID;
        ++g_NextID.id;

        auto scene = std::unique_ptr<Scene>(new Scene(name, new_id));
        Scene& scene_ref = *scene;
        g_ScenePool[new_id] = std::move(scene);

        return scene_ref;
    }

    void SceneManager::destroy_scene(Scene& scene)
    {
        if (g_CurrentScene == &scene)
        {
            ID_ERROR("尝试销毁当前激活的场景 '{}'", scene.get_name());
            return;
        }

        auto it = std::find_if(g_ScenePool.begin(), g_ScenePool.end(),
            [&scene](const auto& pair) { return pair.second.get() == &scene; });

        if (it != g_ScenePool.end())
        {
            g_ScenePool.erase(it);
        }
        else
        {
            ID_WARN("尝试销毁一个不在场景池中的场景 '{}'", scene.get_name());
        }
    }

    void SceneManager::load_scene(Scene& scene)
    {
        // 如果当前场景已经是要加载的场景，则无需切换
        if (g_CurrentScene == &scene) return;

        if(g_CurrentScene) g_CurrentScene->set_paused();

        g_CurrentScene = &scene;
        g_CurrentScene->set_running();

        ID_INFO("已设置场景为 '{}'", g_CurrentScene->get_name());
    }

    Scene& SceneManager::get_current_scene()
    {
        if (!g_CurrentScene)
        {
            load_scene(default_scene);
            // 首次进入默认场景：初始化其材质库（存在则加载，不存在则新建空库）
            AssetManager::load_default_scene_material_library();
        }
        return *g_CurrentScene;
    }

    Scene* SceneManager::find_scene(SceneID scene_id)
    {
        auto it = g_ScenePool.find(scene_id);
        if(it != g_ScenePool.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    void SceneManager::on_update(Timestep ts)
    {
        if (g_CurrentScene) g_CurrentScene->on_update(ts);
        else load_scene(default_scene);
    }

    void SceneManager::on_event(Event& event)
    {
        if (g_CurrentScene) g_CurrentScene->on_event(event);
        else load_scene(default_scene);
    }

    void SceneManager::save(Scene& scene, const std::string& filepath)
    {
        // 确保目标目录存在（如 Assets/scene/）
        std::filesystem::create_directories(std::filesystem::path(filepath).parent_path());

        ArenaID arena = ArenaManager::create_arena();
        const Json json = scene.serialize(arena);
        JSON::write_to_file(filepath, json);
        ArenaManager::destroy_arena(arena);

        ID_INFO("SceneManager::save：场景 '{}' 已保存到 {}", scene.get_name(), filepath);
    }

    Scene& SceneManager::load(const std::string& filepath)
    {
        std::ifstream file(filepath, std::ios::binary);
        if(!file.good())
        {
            ID_ERROR("SceneManager::load：找不到文件 {}", filepath);
            return default_scene;
        }

        const std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        ArenaID arena = ArenaManager::create_arena();
        const Json json = JSON::parse(content, arena);

        // 以文件名（去扩展名）作为场景名；每次加载创建新的场景实例（新 SceneID），
        // 同名文件重复加载互不覆盖，旧实例保留在池中暂停运行
        const std::string name = std::filesystem::path(filepath).stem().string();
        Scene& scene = create_scene(name);
        scene.deserialize(json);
        ArenaManager::destroy_arena(arena);

        ID_INFO("SceneManager::load：已从 {} 加载场景 '{}' ({} 个 GameObject)",
            filepath, scene.get_name(), scene.get_game_object_count());
        return scene;
    }
} // namespace ID