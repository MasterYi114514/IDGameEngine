#pragma once

#include "IDpch.hpp"
#include "Scene/Audio/AudioID.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    class Scene;

    /*
    *   AssetManager 是 ID 引擎层的统一资产入口（纯静态类）。
    *   职责：
    *   - 以 constexpr 字符串存储各资源目录路径（运行时工作目录为 bin/，前缀与 PostProcessPass 的 "../Assets/" 一致）；
    *   - 提供四类资源（audio / scene / shader / texture）的目录枚举与统一加载接口。
    *   DevGUI Panel 只对接本类，不直接调用各子 Manager，保证引擎层与调试层解耦。
    */
    class ID_API AssetManager
    {
    public:
        AssetManager() = delete;
        ~AssetManager() = delete;

    public:
        // ★ constexpr 目录路径（运行时工作目录为 bin/，与 PostProcessPass 的 "../Assets/..." 一致）
        static constexpr const char* AudioDir   = "../Assets/audio/";
        static constexpr const char* SceneDir   = "../Assets/scene/";
        static constexpr const char* ShaderDir  = "../Assets/shader/";
        static constexpr const char* TextureDir = "../Assets/texture/";
        static constexpr const char* MaterialDir = "../Assets/material/";

        // ---- 目录枚举（供 Panel 菜单/列表展示，只返回文件名，不含目录前缀）----
        static std::vector<std::string> list_audios();    // *.wav
        static std::vector<std::string> list_scenes();    // *.json
        static std::vector<std::string> list_shaders();   // *.vsl 的基础名（成对 .vsl/.fsl，缺配对跳过）
        static std::vector<std::string> list_textures();  // *.png 等图片后缀
        static std::vector<std::string> list_material_libraries();  // *.json（全局材质库文件）

        // ---- 统一加载入口（Panel 只对接 AssetManager）----
        static AudioID   load_audio(const std::string& name);    // 内部：AudioManager::load(AudioDir + name)
        static TextureID load_texture(const std::string& name);  // 内部：TextureManager::load_texture(TextureDir + name)
        static ShaderID  load_shader(const std::string& name);   // 内部：ShaderManager::create(ShaderDir + name + ".vsl", ShaderDir + name + ".fsl")
        static Scene&    load_scene(const std::string& name);    // 内部：SceneManager::load(SceneDir + name)，切换由调用方执行

        // ---- 全局材质库持久化（MaterialLibrary 属于全体，不随单个场景序列化）----
        static void save_material_library(const std::string& name);   // 全库保存到 MaterialDir + name
        static void load_material_library(const std::string& name);   // 从 MaterialDir + name 恢复全库
    };
} // namespace ID
