#include "Loader/MaterialLoader.hpp"
#include "Log.hpp"

#include <fstream>
#include <filesystem>
#include "IDJson.hpp"

namespace ID
{
    MaterialData MaterialLoader::load(const std::string& path)
    {
        MaterialData result;

        std::ifstream file(path);
        if(!file.is_open())
        {
            IDASSET_ERROR("MaterialLoader::load：无法打开文件: {}", path);
            return result;
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        std::string json_str = oss.str();
        file.close();

        // 解析 Json 文件
        auto arana_id = ArenaManager::create_arena();
        Json root = JSON::parse(json_str, arana_id);

        if(!root.is_object())
        {
            IDASSET_ERROR("MaterialLoader::load： 解析 Json 文件失败: {}", path);
            ArenaManager::destroy_arena(arana_id);
            return result;
        }

        if(root["shader"].is_string())
        {
            result.shader_name = root["shader"].as_cstr();
        }

        if(root["transparent"].is_bool())
        {
            result.transparent = root["transparent"].as_bool();
        }

        if(root["params"].is_object())
        {
            Json& params = root["params"];

            for(const auto& key : params.get_keys())
            {
                MaterialParamEntry entry;
                entry.name = key;

                Json& value = params[key];
                if(value.is_float())
                {
                    entry.type = "float";
                    entry.value = { static_cast<float>(value.as_float()) };
                }
                else if(value.is_int())
                {
                    entry.type = "int";
                    entry.value = { static_cast<float>(value.as_int()) };
                }
                else if(value.is_array())
                {
                    entry.type = "vec" + std::to_string(value.size());
                    for(size_t i = 0; i < value.size(); ++i)
                    {
                        if(value[i].is_float())
                        {
                            entry.value.push_back(static_cast<float>(value[i].as_float()));
                        }
                        else if(value[i].is_int())
                        {
                            entry.value.push_back(static_cast<float>(value[i].as_int()));
                        }
                        else
                        {
                            IDASSET_ERROR("MaterialLoader::load：不支持的 uniform 参数类型: {}", key);
                        }
                    }
                }
                result.params.push_back(std::move(entry));
            }
        }

        // 解析纹理
        if(root["textures"].is_object())
        {
            Json& textures = root["textures"];

            for(const auto& key : textures.get_keys())
            {
                MaterialTextureEntry entry;
                entry.sampler_name = key;
                entry.texture_path = textures[key].as_cstr();
                entry.slot = 0;         // 默认槽位为 0，TODO：允许扩展槽位
                result.textures.push_back(std::move(entry));
            }
        }

        ArenaManager::destroy_arena(arana_id);
        return result;
    }

    // 保存到 .mat 文件
    void MaterialLoader::save(const MaterialData& data, const std::string& path)
    {
        auto arena_id = ArenaManager::create_arena();
        Json root = Json::create_object(arena_id);

        // shader
        root.insert("shader", Json::create_string(data.shader_name, arena_id));

        // transparent
        root.insert("transparent", Json(data.transparent));

        // params
        Json params = Json::create_object(arena_id);
        for (const auto& p : data.params)
        {
            if (p.value.size() == 1)
            {
                params.insert(p.name, Json(static_cast<double>(p.value[0])));
            }
            else
            {
                Json arr = Json::create_array(arena_id);
                for (float v : p.value)
                {
                    arr.push_back(Json(static_cast<double>(v)));
                }
                params.insert(p.name, arr);
            }
        }
        root.insert("params", params);

        // textures
        Json textures = Json::create_object(arena_id);
        for (const auto& t : data.textures)
        {
            textures.insert(t.sampler_name, Json::create_string(t.texture_path, arena_id));
        }
        root.insert("textures", textures);

        // 写出
        JSON::write_to_file(path, root, JSON::IndentStyle::FourSpace);

        ArenaManager::destroy_arena(arena_id);
    }
} // namespace ID