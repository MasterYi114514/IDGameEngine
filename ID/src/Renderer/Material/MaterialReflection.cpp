#include "Renderer/Material/MaterialReflection.hpp"
#include "Renderer/Resource/ShaderManager.hpp"

#include <cctype>

namespace
{
    /*
    *   引擎保留 uniform 名单（材质不可编辑，由各 RenderPass / Shadow 驱动）。
    *   维护规则：新增 RenderPass 若引入新的引擎侧 set_param uniform，
    *   必须同步补进本名单，否则会泄入 DevGUI 材质参数编辑 UI。
    *   （名单来源：src/Renderer/Render/ 与 src/Renderer/Shadow/ 的 set_param 调用扫描）
    */
    const std::unordered_set<std::string> k_engine_reserved_uniforms = {
        "u_mvp", "u_model", "u_view", "u_proj", "u_projection", "u_camera_pos",
        "u_time", "u_ambient", "u_light_count",
        "u_light_dirs", "u_light_positions", "u_light_colors",
        "u_light_space_mvp", "u_light_view_proj",
        "u_shadow_enabled", "u_shadow_map", "u_shadow_bias", "u_shadow_pcf_radius",
        "u_normal_bias", "u_texel_size",
        "u_sun_dir", "u_sun_intensity",
        "u_use_cubemap", "u_cubemap", "u_top_color", "u_horizon_color", "u_bottom_color",
        "u_input", "u_mode", "u_tone_mapping", "u_gamma", "u_threshold",
        "u_bloom", "u_bloom_strength", "u_has_bloom"
    };

    std::string to_lower(const std::string& text)
    {
        std::string result = text;
        for(char& c : result)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    }

    // ShaderUniformType → MaterialParamType 映射，不可编辑类型返回 None
    ID::MaterialParamType uniform_type_to_param_type(ID::ShaderUniformType type)
    {
        switch(type)
        {
            case ID::ShaderUniformType::Float:  return ID::MaterialParamType::Float;
            case ID::ShaderUniformType::Int:    return ID::MaterialParamType::Int;
            case ID::ShaderUniformType::Vec2:   return ID::MaterialParamType::Vec2;
            case ID::ShaderUniformType::Vec3:   return ID::MaterialParamType::Vec3;
            case ID::ShaderUniformType::Vec4:   return ID::MaterialParamType::Vec4;
            case ID::ShaderUniformType::Mat3:   return ID::MaterialParamType::Mat3;
            case ID::ShaderUniformType::Mat4:   return ID::MaterialParamType::Mat4;
            default:                            return ID::MaterialParamType::None;
        }
    }
} // 匿名命名空间

namespace ID
{
    bool is_engine_reserved_uniform(const std::string& name)
    {
        return k_engine_reserved_uniforms.find(name) != k_engine_reserved_uniforms.end();
    }

    bool looks_like_color_name(const std::string& name)
    {
        const std::string lower = to_lower(name);
        return lower.find("color") != std::string::npos
            || lower.find("colour") != std::string::npos
            || lower.find("tint") != std::string::npos;
    }

    std::vector<EditableParamDesc> get_editable_params(ShaderID shader)
    {
        std::vector<EditableParamDesc> result;

        for(const ShaderUniformDesc& uniform : ShaderManager::get_active_uniforms(shader))
        {
            if(uniform.count > 1) continue;                              // 数组 uniform 不支持编辑
            if(is_engine_reserved_uniform(uniform.name)) continue;      // 引擎保留项

            const MaterialParamType type = uniform_type_to_param_type(uniform.type);
            if(type == MaterialParamType::None) continue;                // Sampler / Bool / Unsupported

            const bool is_color = (type == MaterialParamType::Vec3 || type == MaterialParamType::Vec4)
                && looks_like_color_name(uniform.name);

            result.push_back({ uniform.name, type, is_color });
        }

        return result;
    }

    MaterialParam make_default_param(const EditableParamDesc& desc)
    {
        MaterialParam param;
        param.type = desc.type;         // Array<float,16> 默认值初始化为全 0

        if(desc.is_color)
        {
            // 颜色参数默认白色：Vec3 → (1,1,1)，Vec4 → (1,1,1,1)
            const uint32_t component_count = (desc.type == MaterialParamType::Vec4) ? 4 : 3;
            for(uint32_t i = 0; i < component_count; ++i)
            {
                param.value[i] = 1.0f;
            }
        }

        return param;
    }
} // namespace ID
