#pragma once

#include "IDpch.hpp"
#include "Scene/Audio/AudioID.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    class Scene;

    /*
    *   AssetManager 是 ID 引擎层的统一资产入口（纯静态类）。
    *   职责：
    *   - 以 constexpr 字符串存储各资源目录路径（运行时工作目录为 bin/，前缀与 PostProcessPass 的 "../Assets/" 一致）；
    *   - 提供四类资源（audio / scene / shader / texture）的目录枚举与统一加载/保存接口；
    *   - 场景保存/加载时自动携带材质库：save 前先把当前材质库存到 MaterialDir（与场景同名文件），
    *     load 时优先恢复场景文件中的材质库（先材质库、后 GameObject），并自动保存旧材质库。
    *   DevGUI Panel 只对接本类，不直接调用各子 Manager，保证引擎层与调试层解耦。
    */
    class ID_API AssetManager
    {
    public:
        AssetManager() = delete;
        ~AssetManager() = delete;

    public:
        // ★ constexpr 目录路径（运行时工作目录为 bin/，与 PostProcessPass 的 "../Assets/..." 一致）
        static constexpr const char* AudioDir       = "../Assets/audio/";
        static constexpr const char* SceneDir       = "../Assets/scene/";
        static constexpr const char* ShaderDir      = "../Assets/shader/";
        static constexpr const char* TextureDir     = "../Assets/texture/";
        static constexpr const char* MaterialDir    = "../Assets/material/";
        static constexpr const char* MeshDir        = "../Assets/mesh/";

        // ---- 目录枚举（供 Panel 菜单/列表展示，只返回文件名，不含目录前缀）----
        static std::vector<std::string> list_audios();    // *.wav
        static std::vector<std::string> list_scenes();    // *.json
        static std::vector<std::string> list_shaders();   // *.vsl 的基础名（成对 .vsl/.fsl，缺配对跳过）
        static std::vector<std::string> list_textures();  // *.png 等图片后缀
        static std::vector<std::string> list_material_libraries();  // *.json（全局材质库文件）
        static std::vector<std::string> list_meshes();     // *.obj 等模型后缀

        // ---- 统一加载入口（Panel 只对接 AssetManager）----

        // 内部：AudioManager::load(AudioDir + name)
        static AudioID   load_audio(const std::string& name);

        // 内部：TextureManager::load_texture(TextureDir + name)
        static TextureID load_texture(const std::string& name);  

        // 内部：ShaderManager::create(ShaderDir + name + ".vsl", ShaderDir + name + ".fsl")
        static ShaderID  load_shader(const std::string& name);   

        // 内部：MeshFactory::create_mesh_from_file(MeshDir + name, submesh_index)
        static MeshID    load_mesh(const std::string& name, uint32_t submesh_index = 0);

        /**
         * @brief 枚举模型文件的子网格名称列表
         * @param name Mesh 文件名（不含目录前缀）
         * @return 子网格名列表（aiMesh 名称或 "submesh_N"）；加载失败 / 无子网格返回空列表
         */
        static std::vector<std::string> list_mesh_submeshes(const std::string& name);

        /**
         * @brief 加载文件 Mesh 携带的 diffuse 纹理
         * @param mesh_id 由 load_mesh 得到的 MeshID
         * @return 有效 TextureID；Mesh 无纹理信息 / 纹理加载失败返回 invalid_id
         */
        static TextureID load_mesh_texture(MeshID mesh_id);

        /**
         * @brief 重命名资产磁盘文件
         * @param category "audio" / "shader" / "texture"（未来可扩 "mesh"）
         * @param old_name 旧文件名（不含目录前缀；shader 为基础名）
         * @param new_name 新文件名（shader 为基础名，成对 .vsl/.fsl 同时改名）
         * @return 成功返回 true（目标已存在 / 旧文件不存在 / 非法名字返回 false）
         */
        static bool rename_asset(const std::string& category,
            const std::string& old_name, const std::string& new_name);

        static void      save_scene(Scene& scene, const std::string& name);
        static Scene&    load_scene(const std::string& name);    
        
        static void save_material_library(const std::string& name);   // 全库保存到 MaterialDir + name
        
        static void load_material_library(const std::string& name);   // 从 MaterialDir + name 恢复全库

        // 由场景名生成安全文件名（清理 Windows 非法字符），返回 "<名>.json"
        static std::string scene_filename(const std::string& name);

        // 引擎启动时默认场景（default scene）的材质库初始化：
        // 存在 Assets/material/<默认场景名>.json 则加载，不存在则新建空材质库文件
        static void load_default_scene_material_library();
    };
} // namespace ID
