#include "Renderer/Material/MaterialInstance.hpp"

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
} // namespace ID