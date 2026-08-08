#include "Renderer/Material/Material.hpp"
#include "Renderer/Material/MaterialParam.hpp"
#include "Renderer/Resource/TextureManager.hpp"
#include "Renderer/Resource/ShaderManager.hpp"
#include "Log/Log.hpp"

namespace ID
{
    // TextureBindingDesc ---------------------------------------------------------------------------------
    Json TextureBindingDesc::serialize(ArenaID arena_id) const
    {
        Json json = Json::create_object(arena_id);
        Json jtexpath = Json::create_string(TextureManager::get_texture_path(texture), arena_id);
        json.insert("tex_path", jtexpath);
        json.insert("slot", Json(static_cast<int32_t>(slot)));
        return json;
    }

    void TextureBindingDesc::deserialize(const Json& json)
    {
        texture = TextureManager::load_texture(json["tex_path"].as_cstr());
        slot = static_cast<uint32_t>(json["slot"].as_int());
    }

    // Material -------------------------------------------------------------------------------------------
    Material::Material(ShaderID shader, const std::string& name) : m_shader(shader), m_name(name) { }

    bool Material::has_param(const std::string& name) const
    {
        return m_param_defaults.find(name) != m_param_defaults.end();
    }

    void Material::set_texture(const std::string& name, TextureID texture, uint32_t slot)
    {
        m_texture_defaults[name] = TextureBindingDesc{ texture, slot };
    }

    void Material::set_sampler(const std::string& name, uint32_t slot)
    {
        m_texture_defaults[name] = TextureBindingDesc{ TextureID::invalid_id(), slot };
    }

    void Material::apply() const
    {
        for (const auto& [name, param] : m_param_defaults)
        {
            apply_param(m_shader, name, param);
        }

        for (const auto& [name, binding] : m_texture_defaults)
        {
            // 采样器槽位是 int uniform
            IDRCmd::set_param(m_shader, name, static_cast<int>(binding.slot));
            if (binding.texture.is_valid())
            {
                IDRCmd::bind_texture(binding.texture, binding.slot);
            }
        }
    }

    void Material::apply_param(ShaderID shader, const std::string& name, const MaterialParam& param)
    {
        if (!param.is_valid())
        {
            return;
        }

        switch (param.type)
        {
            case MaterialParamType::Float:
                IDRCmd::set_param(shader, name, param.value[0]);
                break;
            case MaterialParamType::Int:
                IDRCmd::set_param(shader, name, static_cast<int>(param.value[0]));
                break;
            case MaterialParamType::Vec2:
                IDRCmd::set_param(shader, name, param.value[0], param.value[1]);
                break;
            case MaterialParamType::Vec3:
                IDRCmd::set_param(shader, name, param.value[0], param.value[1], param.value[2]);
                break;
            case MaterialParamType::Vec4:
                IDRCmd::set_param(shader, name, param.value[0], param.value[1], param.value[2], param.value[3]);
                break;
            case MaterialParamType::Mat3:
                IDRCmd::set_param(shader, name, 3U, 3U, param.value.data);
                break;
            case MaterialParamType::Mat4:
                IDRCmd::set_param(shader, name, 4U, 4U, param.value.data);
                break;
            default:
                ID_ERROR("Material::apply_param 进入到不应存在的分支，请检查代码逻辑");
                break;
        }
    }

    Json Material::serialize(ArenaID arena_id) const
    {
        Json result = Json::create_object(arena_id);
        result.insert("shader_id", ShaderManager::serialize_shader(m_shader, arena_id));
        result.insert("name", Json::create_string(m_name, arena_id));
        
        Json jparam = Json::create_object(arena_id);
        for(const auto& [name, param] : m_param_defaults)
        {
            jparam.insert(name, param.serialize(arena_id));
        }
        result.insert("param_defaults", jparam);

        Json jtexture = Json::create_object(arena_id);
        for(const auto& [name, binding] : m_texture_defaults)
        {
            jtexture.insert(name, binding.serialize(arena_id));
        }
        result.insert("texture_defaults", jtexture);
        return result;
    }

    void Material::deserialize(const Json& json)
    {
        m_shader = ShaderManager::deserialize_shader(json["shader_id"]);
        m_name = json["name"].as_cstr();

        const Json& jparam = json["param_defaults"];
        m_param_defaults.clear();
        if(jparam.is_object())
        {
            for(const auto& key : jparam.get_keys())
            {
                MaterialParam param;
                param.deserialize(jparam[key]);
                m_param_defaults[key] = param;
            }
        }

        m_texture_defaults.clear();
        const Json& jtexture = json["texture_defaults"];
        if(jtexture.is_object())
        {
            for(const auto& key : jtexture.get_keys())
            {
                TextureBindingDesc binding;
                binding.deserialize(jtexture[key]);
                m_texture_defaults[key] = binding;
            }
        }
    }
} // namespace ID