#pragma once

#include "IDpch.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

namespace ID
{
    /*
    *   AssetPanel — 资源浏览器面板（Asset Browser）
    *
    *   - 四分类（Audio / Scene / Shader / Texture）：目录枚举 + Combo 菜单选择 + Load 按钮；
    *   - Scene 区：文件名输入框 + Save Current Scene 键 + Combo + Load 键
    *     （Save 把当前场景保存到 Assets/scene/，Load 从 Assets/scene/ 加载并切换场景）；
    *   - 目录文件列表与已加载资源列表均可作为拖拽源（DragDropSource），
    *     拖拽目标在 Inspector（AudioSource / MeshRenderer 编辑器）。
    *
    *   只对接 AssetManager，不直接调用各子 Manager。
    */
    class ID_API AssetPanel : public ImGuiPanel
    {
    public:
        AssetPanel();
        void on_imgui_render() override;

    private:
        // 各分类分区渲染
        void render_audio_section();
        void render_scene_section();
        void render_shader_section();
        void render_texture_section();
        void render_material_section();
        void render_loaded_list();

        // 渲染 Combo 下拉，返回当前选中文件名（空 = 未选择）
        std::string render_combo(const char* combo_id,
            const std::vector<std::string>& files, std::string& selected_name);

        // 渲染目录文件列表，每项挂 DragDropSource（payload 数据为文件名）
        void render_drag_list(const char* payload_type, const std::vector<std::string>& files);

        // 已加载资源记录（每项同样可作为拖拽源）
        struct LoadedAsset
        {
            std::string category;   // "audio" / "shader" / "texture"
            std::string name;       // 文件名（shader 为基础名）
            std::string path;       // 完整路径
            uint32_t    id = 0;     // 句柄 id
        };

        void add_loaded_asset(const std::string& category, const std::string& name,
            const std::string& path, uint32_t id);

    private:
        std::string m_selected_audio_name;      // 各分类 Combo 选择缓存
        std::string m_selected_scene_name;
        std::string m_selected_shader_name;
        std::string m_selected_texture_name;
        std::string m_selected_material_shader; // 材质创建用的 shader Combo 选择缓存
        std::string m_selected_material_lib;    // 材质库文件 Combo 选择缓存

        char m_material_name[64] = "NewMaterial";   // 新材质名称输入框
        char m_material_lib_name[64] = "library.json"; // 材质库保存文件名输入框

        std::vector<LoadedAsset> m_loaded_assets;   // 已加载资源列表
    };
} // namespace ID
