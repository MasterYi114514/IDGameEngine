#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    /*
    *   SceneSettingsPanel — 场景设置面板
    *
    *   - 场景名称显示/修改
    *   - Running / Paused 状态切换
    *   - 场景池列表（本会话创建过的场景），支持 Load / Destroy / Create
    *
    *   说明：SceneManager 未提供场景池枚举接口，Panel 内部记录本会话
    *   创建过的场景名称；Load 通过 create_scene(name)（已存在时返回旧场景）实现。
    */
    class ID_API SceneSettingsPanel : public ImGuiPanel
    {
    public:
        SceneSettingsPanel();
        void on_imgui_render() override;

    private:
        void render_scene_info();
        void render_scene_pool();

        // 记录本会话创建过的场景名称（去重）
        void remember_scene_name(const std::string& name);

    private:
        std::vector<std::string> m_scene_names;
        char m_new_scene_name[64] = "New Scene";
    };
} // namespace ID
