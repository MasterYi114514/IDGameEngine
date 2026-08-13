#include "Scene/AssetManager.hpp"
#include "Scene/Audio/AudioManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Renderer/Resource/TextureManager.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"
#include "IDJson.hpp"
#include "Log/Log.hpp"

#include <fstream>

namespace
{
    // 枚举指定目录下满足扩展名集合的文件名（只返回文件名，不含目录前缀）
    std::vector<std::string> list_files_by_extensions(const char* dir, const std::vector<std::string>& extensions)
    {
        std::vector<std::string> result;
        // 目录不存在或为空都是合法状态（首次使用前尚未创建/尚无资源），静默返回空
        if(!std::filesystem::exists(dir))
        {
            return result;
        }

        for(const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if(!entry.is_regular_file()) continue;
            const std::string ext = entry.path().extension().string();
            for(const auto& e : extensions)
            {
                if(ext == e)
                {
                    result.push_back(entry.path().filename().string());
                    break;
                }
            }
        }
        return result;
    }
} // 匿名命名空间

namespace ID
{
    std::vector<std::string> AssetManager::list_audios()
    {
        return list_files_by_extensions(AudioDir, { ".wav" });
    }

    std::vector<std::string> AssetManager::list_scenes()
    {
        return list_files_by_extensions(SceneDir, { ".json" });
    }

    std::vector<std::string> AssetManager::list_shaders()
    {
        std::vector<std::string> result;
        if(!std::filesystem::exists(ShaderDir))
        {
            return result;
        }

        for(const auto& entry : std::filesystem::directory_iterator(ShaderDir))
        {
            if(!entry.is_regular_file()) continue;
            if(entry.path().extension().string() != ".vsl") continue;

            // shader 资源按基础名成对列出，缺同名 .fsl 时告警跳过
            const std::string base = entry.path().stem().string();
            if(!std::filesystem::exists(std::string(ShaderDir) + base + ".fsl"))
            {
                ID_WARN("AssetManager：shader '{}' 缺少同名 .fsl，已跳过", base);
                continue;
            }
            result.push_back(base);
        }
        return result;
    }

    std::vector<std::string> AssetManager::list_textures()
    {
        return list_files_by_extensions(TextureDir, { ".png", ".jpg", ".jpeg", ".bmp", ".tga" });
    }

    std::vector<std::string> AssetManager::list_material_libraries()
    {
        return list_files_by_extensions(MaterialDir, { ".json" });
    }

    AudioID AssetManager::load_audio(const std::string& name)
    {
        return AudioManager::load(std::string(AudioDir) + name);
    }

    TextureID AssetManager::load_texture(const std::string& name)
    {
        return TextureManager::load_texture(std::string(TextureDir) + name);
    }

    ShaderID AssetManager::load_shader(const std::string& name)
    {
        return ShaderManager::create(
            std::string(ShaderDir) + name + ".vsl",
            std::string(ShaderDir) + name + ".fsl");
    }

    Scene& AssetManager::load_scene(const std::string& name)
    {
        return SceneManager::load(std::string(SceneDir) + name);
    }

    void AssetManager::save_material_library(const std::string& name)
    {
        // 确保目标目录存在
        std::filesystem::create_directories(MaterialDir);

        ArenaID arena = ArenaManager::create_arena();
        const Json json = MaterialLibrary::serialize_all(arena);
        JSON::write_to_file(std::string(MaterialDir) + name, json);
        ArenaManager::destroy_arena(arena);

        ID_INFO("AssetManager：全局材质库已保存到 {}{}", MaterialDir, name);
    }

    void AssetManager::load_material_library(const std::string& name)
    {
        const std::string filepath = std::string(MaterialDir) + name;
        std::ifstream file(filepath, std::ios::binary);
        if(!file.good())
        {
            ID_ERROR("AssetManager：找不到材质库文件 {}", filepath);
            return;
        }

        const std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        ArenaID arena = ArenaManager::create_arena();
        const Json json = JSON::parse(content, arena);
        MaterialLibrary::deserialize_all(json);
        ArenaManager::destroy_arena(arena);

        ID_INFO("AssetManager：全局材质库已从 {} 加载（{} 个材质）", filepath, MaterialLibrary::size());
    }
} // namespace ID
