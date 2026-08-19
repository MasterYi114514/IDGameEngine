#include "Scene/AssetManager.hpp"
#include "Scene/Audio/AudioManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Renderer/Resource/TextureManager.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"
#include "IDJson.hpp"
#include "Log/Log.hpp"
#include "Loader/MeshLoader.hpp"

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

    std::vector<std::string> AssetManager::list_meshes()
    {
        return list_files_by_extensions(MeshDir, { ".obj", ".fbx", ".gltf", ".glb", ".dae" });
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

    MeshID AssetManager::load_mesh(const std::string& name, uint32_t submesh_index)
    {
        return MeshFactory::create_mesh_from_file(std::string(MeshDir) + name, submesh_index);
    }

    std::vector<std::string> AssetManager::list_mesh_submeshes(const std::string& name)
    {
        // 每次调用完整解析模型文件（Assimp，约几十 ms）；仅由 Inspector 子网格 Combo 展开时调用一次，帧循环不调用
        MeshLoadResult result = MeshLoader::load_meshes(std::string(MeshDir) + name);
        return std::move(result.mesh_names);
    }

    TextureID AssetManager::load_mesh_texture(MeshID mesh_id)
    {
        const MeshSourceDesc* desc = MeshFactory::get_source_desc(mesh_id);
        if(!desc)
        {
            ID_WARN("AssetManager::load_mesh_texture：MeshID {} 无来源信息", mesh_id.get_id());
            return TextureID::invalid_id();
        }
        if(desc->source_type != MeshSourceType::File)
        {
            ID_WARN("AssetManager::load_mesh_texture：MeshID {} 非文件来源，无纹理", mesh_id.get_id());
            return TextureID::invalid_id();
        }
        if(desc->texture_path.empty())
        {
            ID_WARN("AssetManager::load_mesh_texture：MeshID {} 的文件 Mesh 未携带纹理", mesh_id.get_id());
            return TextureID::invalid_id();
        }

        TextureID tex = TextureManager::load_texture(desc->texture_path);
        if(!tex.is_valid())
        {
            ID_ERROR("AssetManager::load_mesh_texture：纹理加载失败: {}", desc->texture_path);
        }
        return tex;
    }

    bool AssetManager::rename_asset(const std::string& category,
        const std::string& old_name, const std::string& new_name)
    {
        // 名字合法性：非空且不含 Windows 非法字符（字符集与 scene_filename 一致）
        if(new_name.empty())
        {
            ID_WARN("AssetManager：重命名失败，新名字不能为空");
            return false;
        }
        for(char c : new_name)
        {
            if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
                || c == '"' || c == '<' || c == '>' || c == '|')
            {
                ID_WARN("AssetManager：重命名失败，新名字 '{}' 含非法字符", new_name);
                return false;
            }
        }

        // 重命名单文件：旧文件缺失 / 目标已存在 / 系统错误均返回 false
        auto rename_file = [](const std::string& old_path, const std::string& new_path) -> bool
        {
            if(!std::filesystem::exists(old_path))
            {
                ID_ERROR("AssetManager：重命名失败，旧文件不存在 {}", old_path);
                return false;
            }
            if(std::filesystem::exists(new_path))
            {
                ID_WARN("AssetManager：重命名失败，目标已存在 {}", new_path);
                return false;
            }
            std::error_code ec;
            std::filesystem::rename(old_path, new_path, ec);
            if(ec)
            {
                ID_ERROR("AssetManager：重命名失败 {}（{}）", old_path, ec.message());
                return false;
            }
            return true;
        };

        if(category == "audio")
        {
            if(rename_file(std::string(AudioDir) + old_name, std::string(AudioDir) + new_name))
            {
                ID_INFO("AssetManager：音频 '{}' 已重命名为 '{}'", old_name, new_name);
                return true;
            }
        }
        else if(category == "texture")
        {
            if(rename_file(std::string(TextureDir) + old_name, std::string(TextureDir) + new_name))
            {
                ID_INFO("AssetManager：纹理 '{}' 已重命名为 '{}'", old_name, new_name);
                return true;
            }
        }
        else if(category == "shader")
        {
            // shader 成对改名：先整体检查 .vsl/.fsl 的旧文件与目标，再分别 rename
            const std::string old_vsl = std::string(ShaderDir) + old_name + ".vsl";
            const std::string old_fsl = std::string(ShaderDir) + old_name + ".fsl";
            const std::string new_vsl = std::string(ShaderDir) + new_name + ".vsl";
            const std::string new_fsl = std::string(ShaderDir) + new_name + ".fsl";
            if(!std::filesystem::exists(old_vsl) || !std::filesystem::exists(old_fsl))
            {
                ID_ERROR("AssetManager：重命名失败，shader '{}' 的 .vsl/.fsl 不完整", old_name);
                return false;
            }
            if(std::filesystem::exists(new_vsl) || std::filesystem::exists(new_fsl))
            {
                ID_WARN("AssetManager：重命名失败，shader 目标 '{}' 已存在", new_name);
                return false;
            }
            if(!rename_file(old_vsl, new_vsl))
            {
                return false;
            }
            if(!rename_file(old_fsl, new_fsl))
            {
                // 第二个文件失败时回滚第一个，避免半改状态
                std::error_code ec;
                std::filesystem::rename(new_vsl, old_vsl, ec);
                ID_ERROR("AssetManager：重命名失败，shader '{}' 改名已回滚", old_name);
                return false;
            }
            ID_INFO("AssetManager：shader '{}' 已重命名为 '{}'（.vsl/.fsl 成对改名）", old_name, new_name);
            return true;
        }
        else
        {
            ID_WARN("AssetManager：不支持重命名分类 '{}'", category);
        }
        return false;
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
