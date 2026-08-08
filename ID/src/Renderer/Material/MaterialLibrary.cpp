#include "Renderer/Material/MaterialLibrary.hpp"
#include "Renderer/Resource/TextureManager.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Log/Log.hpp"


#define MLib ::ID::MaterialLibrary::storage()

namespace ID
{
    std::vector<std::unique_ptr<Material>>& MaterialLibrary::storage()
    {
        static std::vector<std::unique_ptr<Material>> s_storage;
        return s_storage;
    }

    Material* MaterialLibrary::add(ShaderID shader, const std::string& name)
    {
        auto& storage = MLib;

        // 重名：幂等返回已存在项
        for (const auto& material : storage)
        {
            if (material->get_name() == name)
            {
                return material.get();
            }
        }

        storage.push_back(std::make_unique<Material>(shader, name));
        return storage.back().get();
    }

    Material* MaterialLibrary::get(const std::string& name)
    {
        for (const auto& material : MLib)
        {
            if (material->get_name() == name)
            {
                return material.get();
            }
        }
        return nullptr;
    }

    bool MaterialLibrary::contains(const std::string& name)
    {
        return get(name) != nullptr;
    }

    void MaterialLibrary::remove(const std::string& name)
    {
        auto& storage = MLib;
        storage.erase(std::remove_if(storage.begin(), storage.end(),
            [&name](const std::unique_ptr<Material>& material) {
                return material->get_name() == name;
            }), 
            storage.end());
    }

    void MaterialLibrary::clear()
    {
        MLib.clear();
    }

    size_t MaterialLibrary::size()
    {
        return MLib.size();
    }

    Json MaterialLibrary::serialize(const std::string& name, ArenaID arena_id)
    {
        Material* material = get(name);
        if (!material)
        {
            ID_ERROR("MaterialLibrary::serialize: 尝试给不存在的材质 {} 序列化", name);
            return JSON::null;
        }
        
        Json result = Json::create_object(arena_id);
        result.insert("name", Json::create_string(name, arena_id));
        result.insert("info", material->serialize(arena_id));

        return result;
    }

    Material* MaterialLibrary::deserialize(const Json& json)
    {
        if (!json.is_object())
        {
            ID_ERROR("MaterialLibrary::deserialize: json 不是对象类型，无法反序列化材质");
            return nullptr;
        }

        std::string name = json["name"].as_cstr();
        const Json& info = json["info"];

        Material* material = get(name);
        if (!material)
        {
            material = add(ShaderID::invalid_id(), name);
        }
        material->deserialize(info);
        return material;
    }
} // namespace ID