#include "IDpch.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include "Log/Log.hpp"

namespace
{
    std::map<std::string, std::unique_ptr<ID::Scene>> g_ScenePool;
    ID::Scene* g_CurrentScene = nullptr;
} // 匿名命名空间

namespace ID
{
    Scene SceneManager::default_scene{"Default Scene"};

    Scene& SceneManager::create_scene(const std::string& name)
    {
        if (g_ScenePool.find(name) != g_ScenePool.end())
        {
            ID_WARN("名叫 '{}' 的场景已经存在，create_scene 方法已返回该场景", name);
            return *g_ScenePool[name];
        }

        auto scene = std::unique_ptr<Scene>(new Scene(name));
        Scene& scene_ref = *scene;
        g_ScenePool[name] = std::move(scene);

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
        if (!g_CurrentScene) load_scene(default_scene);
        return *g_CurrentScene;
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
} // namespace ID