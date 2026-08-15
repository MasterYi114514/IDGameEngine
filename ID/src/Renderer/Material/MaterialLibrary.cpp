#include "Renderer/Material/MaterialLibrary.hpp"
#include "Renderer/Resource/TextureManager.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Log/Log.hpp"


#define MLib ::ID::MaterialLibrary::storage()

namespace
{
    // 材质库静态存储：程序退出阶段析构时打印汇总日志，确认所有材质已释放
    struct MaterialStorage
    {
        std::vector<std::unique_ptr<ID::Material>> data;

        ~MaterialStorage()
        {
            ID_INFO("材质库已销毁：{} 个材质（参数、纹理绑定描述）已全部释放", data.size());
        }
    };
} // 匿名命名空间

namespace ID
{
    std::vector<std::unique_ptr<Material>>& MaterialLibrary::storage()
    {
        static MaterialStorage s_storage;
        return s_storage.data;
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

    std::vector<Material*> MaterialLibrary::get_all()
    {
        std::vector<Material*> result;
        result.reserve(MLib.size());
        for (const auto& material : MLib)
        {
            result.push_back(material.get());
        }
        return result;
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

    Json MaterialLibrary::serialize_all(ArenaID arena_id)
    {
        Json result = Json::create_array(arena_id);
        for (const auto& material : MLib)
        {
            Json item = Json::create_object(arena_id);
            item.insert("name", Json::create_string(material->get_name(), arena_id));
            item.insert("info", material->serialize(arena_id));
            result.push_back(item);
        }
        return result;
    }

    void MaterialLibrary::deserialize_all(const Json& json)
    {
        if (!json.is_array())
        {
            // 旧场景文件没有 materials 字段，静默跳过（保持向后兼容）
            return;
        }
        for (size_t i = 0; i < json.size(); ++i)
        {
            deserialize(json[i]);
        }
    }
} // namespace ID