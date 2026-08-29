#include "Renderer/Resource/ShaderManager.hpp"
#include "IDAsset.hpp"
#include "Log/Log.hpp"

namespace
{
    using StrPair = std::pair<std::string, std::string>;    // vertex_path, fragment_path
    std::unordered_map<ID::ShaderUINT, StrPair> path_map;
} // 匿名命名空间

namespace ID
{
    ShaderID ShaderManager::create(const std::string& vertex_path, const std::string& fragment_path)
    {
        std::string vertex_source = ShaderSourceLoader::load_shader_source(vertex_path);
        std::string fragment_source = ShaderSourceLoader::load_shader_source(fragment_path);
        ShaderCreateInfo info{ vertex_source, fragment_source };
        ShaderID shader_id = ::ShaderManager::create(info);

        path_map[shader_id.get_id()] = { vertex_path, fragment_path };

        return shader_id;
    }

    std::string ShaderManager::get_vertex_shader_path(ShaderID shader_id)
    {
        auto it = path_map.find(shader_id.get_id());
        if (it != path_map.end())
        {
            return it->second.first;
        }
        return {};
    }

    std::string ShaderManager::get_fragment_shader_path(ShaderID shader_id)
    {
        auto it = path_map.find(shader_id.get_id());
        if (it != path_map.end())
        {
            return it->second.second;
        }
        return {};
    }

    std::vector<ShaderUniformDesc> ShaderManager::get_active_uniforms(ShaderID shader_id)
    {
        // 注意：全局 ::ShaderManager（IDRenderer 别名）被本类遮蔽，需全局限定
        return ::ShaderManager::get_active_uniforms(shader_id);
    }

    Json ShaderManager::serialize_shader(ShaderID shader_id, ArenaID arena_id)
    {
        Json json = Json::create_object(arena_id);
        json.insert("ver_path", Json::create_string(get_vertex_shader_path(shader_id), arena_id));
        json.insert("fra_path", Json::create_string(get_fragment_shader_path(shader_id), arena_id));
        return json;
    }

    ShaderID ShaderManager::deserialize_shader(const Json& json)
    {
        std::string ver_path = json["ver_path"].as_cstr();
        std::string fra_path = json["fra_path"].as_cstr();
        return create(ver_path, fra_path);
    }
}