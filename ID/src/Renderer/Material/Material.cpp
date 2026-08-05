#include "Renderer/Material/Material.hpp"
#include "Renderer/Material/MaterialParam.hpp"
#include "Log/Log.hpp"

namespace ID
{
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
} // namespace ID