#pragma once

#include "IDpch.hpp"
#include "Scene/SceneID.hpp"

namespace ID
{
    class Scene;
    class Event;

    /**
     *  只允许设置新的 Scene，不允许直接销毁当前 Scene
     *  确保 get_current_scene() 永远返回一个有效的 Scene 引用
     */
    class ID_API SceneManager
    {
    public:
        SceneManager() = delete;
        ~SceneManager() = delete;

    public:
        // 创建一个新的 Scene，并返回其引用
        static Scene& create_scene(const std::string& name);

        // 销毁一个 Scene，不允许销毁当前激活的 Scene
        static void destroy_scene(Scene& scene);

        // 切换到指定的 Scene（之前的 Scene 暂停更新）
        static void load_scene(Scene& scene);

        // 获取当前激活的 Scene
        static Scene& get_current_scene();

        // 按 SceneID 查询场景；不存在（已销毁/从未创建）返回 nullptr
        static Scene* find_scene(SceneID scene_id);

        // 更新当前激活的 Scene
        static void on_update(Timestep ts);

        // 处理事件
        static void on_event(Event& event);

        // 将 scene 序列化到指定文件路径
        static void save(Scene& scene, const std::string& filepath);

        // 从指定文件路径加载 scene
        static Scene& load(const std::string& filepath);

    private:
        static Scene default_scene;      // 默认场景
    };
} // namespace ID