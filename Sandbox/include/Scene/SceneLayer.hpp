#pragma once

#include "ID.hpp"
#include <Log/Log.hpp>

using namespace ID;

/*
*   SceneLayer — 场景驱动层
*
*   不创建/持有任何固定场景：场景内容由用户通过 DevGUI
*   （Scene Settings / Asset Browser）创建、加载与切换；
*   本 Layer 只负责每帧驱动 SceneManager 的当前激活场景更新。
*/
class SceneLayer : public Layer
{
public:
    SceneLayer() : Layer("SceneLayer") { }

    void on_attach() override
    {
        ID_TRACE("[SceneLayer] on_attach 开始");
        // 从 SceneManager 获取当前激活场景（首次调用会初始化默认场景）
        Scene& scene = SceneManager::get_current_scene();
        ID_TRACE("[SceneLayer] on_attach 完成，当前场景: '{}' (id={})", scene.get_name(), scene.get_id().id);
    }

    void on_update(Timestep ts) override
    {
        // 驱动物理模拟等场景更新——始终作用于当前激活场景（切换后自动跟随）
        SceneManager::on_update(ts);
    }

    void on_event(Event& event) override
    {
        // SceneManager::on_event(event);
    }
};
