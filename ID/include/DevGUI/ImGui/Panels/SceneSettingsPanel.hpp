#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"
#include "Scene/SceneID.hpp"

namespace ID
{
    class Scene;
    /*
    *   SceneSettingsPanel — 场景设置面板
    *
    *   - 场景名称显示/修改（名字只是名字，不承担场景身份）
    *   - Running / Paused 状态切换
    *   - 场景池列表（本会话创建过的场景），支持 Load / Destroy / Create
    *
    *   说明：SceneManager 未提供场景池枚举接口，Panel 内部按 SceneID 记录
    *   本会话创建过的场景；Load / Destroy 通过 SceneManager::find_scene 按 ID 操作。
    */
    class ID_API SceneSettingsPanel : public ImGuiPanel
    {
    public:
        SceneSettingsPanel();
        void on_imgui_render() override;

    private:
        void render_scene_info();
        void render_scene_pool();

        // 记录本会话创建过的场景（按 SceneID 去重）
        void remember_scene(const Scene& scene);

    private:
        std::vector<SceneID> m_scene_ids;
        char m_new_scene_name[64] = "New Scene";
    };
} // namespace ID
