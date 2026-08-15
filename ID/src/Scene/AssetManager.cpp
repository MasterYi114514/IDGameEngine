#include "Scene/AssetManager.hpp"
#include "Scene/Audio/AudioManager.hpp"
#include "Scene/Scene.hpp"
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
        const std::string filepath = std::string(SceneDir) + name;

        // 1) 自动保存旧材质库到 MaterialDir（与旧场景同名文件），避免切换场景后丢失
        const Scene& old_scene = SceneManager::get_current_scene();
        save_material_library(scene_filename(old_scene.get_name()));

        // 2) 读取场景文件，优先恢复材质库（MeshRenderer 等反序列化需要材质库已就绪）
        std::ifstream file(filepath, std::ios::binary);
        if(!file.good())
        {
            ID_ERROR("AssetManager：找不到场景文件 {}", filepath);
            return SceneManager::get_current_scene();
        }

        const std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        ArenaID arena = ArenaManager::create_arena();
        const Json json = JSON::parse(content, arena);

        // 旧版场景文件没有 material_library 字段：保持当前材质库（向后兼容）
        if(json.contains("material_library"))
        {
            MaterialLibrary::clear();
            MaterialLibrary::deserialize_all(json["material_library"]);
        }
        else
        {
            ID_WARN("AssetManager：场景 {} 无 material_library 字段（旧版文件），保持当前材质库", name);
        }
        ArenaManager::destroy_arena(arena);

        // 3) 创建并返回新场景（材质库已就绪，场景内材质引用可正确恢复）
        return SceneManager::load(filepath);
    }

    void AssetManager::save_scene(Scene& scene, const std::string& name)
    {
        // 1) 自动保存当前材质库到 MaterialDir（与场景文件同名）
        save_material_library(name);

        // 2) 序列化场景，携带 material_library 部分（加载时优先恢复材质库，再恢复 GameObject）
        std::filesystem::create_directories(SceneDir);

        ArenaID arena = ArenaManager::create_arena();
        Json result = Json::create_object(arena);

        result.insert("material_library", MaterialLibrary::serialize_all(arena));

        const Json scene_json = scene.serialize(arena);
        result.insert("name", scene_json["name"]);
        result.insert("game_objects", scene_json["game_objects"]);

        JSON::write_to_file(std::string(SceneDir) + name, result);
        ArenaManager::destroy_arena(arena);

        ID_INFO("AssetManager：场景 '{}' 已保存到 {}{}（材质库同步保存到 {}{}）",
            scene.get_name(), SceneDir, name, MaterialDir, name);
    }

    std::string AssetManager::scene_filename(const std::string& name)
    {
        std::string filename = name;
        for(char& c : filename)
        {
            if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
                || c == '"' || c == '<' || c == '>' || c == '|')
            {
                c = '_';
            }
        }
        filename += ".json";
        return filename;
    }

    void AssetManager::load_default_scene_material_library()
    {
        // 当前场景（首次进入时即 default scene）对应的材质库文件
        const std::string lib_name = scene_filename(SceneManager::get_current_scene().get_name());
        const std::string filepath = std::string(MaterialDir) + lib_name;

        if(std::filesystem::exists(filepath))
        {
            load_material_library(lib_name);
        }
        else
        {
            // 不存在：新建空的材质库文件（MaterialLibrary 此刻为空，直接落盘）
            save_material_library(lib_name);
            ID_INFO("AssetManager：默认场景材质库不存在，已新建空库 {}", filepath);
        }
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
