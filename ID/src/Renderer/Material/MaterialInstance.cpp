#include "Renderer/Material/MaterialInstance.hpp"
#include "Renderer/Resource/TextureManager.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"

#include "Log/Log.hpp"

namespace ID
{
    void MaterialInstance::set_texture(const std::string& name, TextureID texture, uint32_t slot)
    {
        m_texture_overrides[name] = TextureBindingDesc{ texture, slot };
    }

    void MaterialInstance::set_sampler(const std::string& name, uint32_t slot)
    {
        m_texture_overrides[name] = TextureBindingDesc{ TextureID::invalid_id(), slot };
    }

    void MaterialInstance::apply() const
    {
        if(!is_valid()) return;

        ShaderID shader = m_parent->get_shader();

        // 1. 父级默认值
        for(const auto [name, param] : m_parent->get_param_defaults())
        {
            Material::apply_param(shader, name, param);
        }
        
        for(const auto [name, binding] : m_parent->get_texture_defaults())
        {
            IDRCmd::set_param(shader, name, static_cast<int>(binding.slot));
            if(binding.texture.is_valid())
            {
                IDRCmd::bind_texture(binding.texture, binding.slot);
            }
        }

        // 2. 局部覆盖
        for(const auto [name, param] : m_overrides)
        {
            Material::apply_param(shader, name, param);
        }

        for(const auto [name, binding] : m_texture_overrides)
        {
            IDRCmd::set_param(shader, name, static_cast<int>(binding.slot));
            if(binding.texture.is_valid())
            {
                IDRCmd::bind_texture(binding.texture, binding.slot);
            }
        }
    }

    Json MaterialInstance::serialize(ArenaID arena_id) const
    {
        Json result = Json::create_object(arena_id);
        result.insert("parent_name", Json::create_string(m_parent ? m_parent->get_name() : "<invalid>", arena_id));

        Json jparam = Json::create_object(arena_id);
        for(const auto& [name, param] : m_overrides)
        {
            jparam.insert(name, param.serialize(arena_id));
        }
        result.insert("param_overrides", jparam);

        Json jtexture = Json::create_object(arena_id);
        for(const auto& [name, binding] : m_texture_overrides)
        {
            jtexture.insert(name, binding.serialize(arena_id));
        }
        result.insert("texture_overrides", jtexture);
        return result;
    }

    void MaterialInstance::deserialize(const Json& json)
    {
        if (!json.is_object())
        {
            ID_ERROR("MaterialInstance::deserialize: json 不是对象类型，无法反序列化材质实例");
            return;
        }

        std::string parent_name = json["parent_name"].as_cstr();
        m_parent = MaterialLibrary::get(parent_name);
        if (!m_parent)
        {
            ID_ERROR("MaterialInstance::deserialize: 找不到父级材质 {}", parent_name);
            return;
        }

        const Json& jparam = json["param_overrides"];
        m_overrides.clear();
        if (jparam.is_object())
        {
            for (const auto& key : jparam.get_keys())
            {
                MaterialParam param;
                param.deserialize(jparam[key]);
                m_overrides[key] = param;
            }
        }

        const Json& jtexture = json["texture_overrides"];
        m_texture_overrides.clear();
        if (jtexture.is_object())
        {
            for (const auto& key : jtexture.get_keys())
            {
                TextureBindingDesc binding;
                binding.deserialize(jtexture[key]);
                m_texture_overrides[key] = binding;
            }
        }
    }
} // namespace ID