#include "Renderer/Material/MaterialLibrary.hpp"

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
} // namespace ID