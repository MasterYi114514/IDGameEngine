#include "DevGUI/ImGui/Widgets/MaterialParamWidget.hpp"

namespace
{
    // MaterialParamType 枚举顺序：None=0, Int, Float, Vec2, Vec3, Vec4, Mat3, Mat4
    constexpr const char* k_param_type_tags[] =
    {
        "None", "Int", "Float", "Vec2", "Vec3", "Vec4", "Mat3", "Mat4"
    };
    constexpr uint32_t k_param_type_tag_count = 8;
} // 匿名命名空间

namespace ID
{
    const char* material_param_type_tag(MaterialParamType type)
    {
        const uint32_t index = static_cast<uint32_t>(type);
        return (index < k_param_type_tag_count) ? k_param_type_tags[index] : "None";
    }

    bool draw_material_param_editor(MaterialParam& param, bool is_color)
    {
        // 直接编辑 MaterialParam::value 的 float 数组（Array<float,16> 的裸内存）
        float* v = param.value.data;

        switch(param.type)
        {
            case MaterialParamType::Float:
                return ImGui::DragFloat("##v", &v[0], 0.01f);

            case MaterialParamType::Int:
            {
                // int 存为 float（应用时转回），UI 用局部 int 缓冲呈现整数语义
                int int_value = static_cast<int>(v[0]);
                if(ImGui::InputInt("##v", &int_value))
                {
                    v[0] = static_cast<float>(int_value);
                    return true;
                }
                return false;
            }

            case MaterialParamType::Vec2:
                return ImGui::DragFloat2("##v", v, 0.01f);

            case MaterialParamType::Vec3:
                return is_color ? ImGui::ColorEdit3("##v", v)
                                : ImGui::DragFloat3("##v", v, 0.01f);

            case MaterialParamType::Vec4:
                return is_color ? ImGui::ColorEdit4("##v", v)
                                : ImGui::DragFloat4("##v", v, 0.01f);

            case MaterialParamType::Mat3:
            case MaterialParamType::Mat4:
            {
                // 矩阵按 GL 列主序内存顺序直接展示（与 IDMath::Mat3/Mat4::get_data() 一致）
                const uint32_t element_count = (param.type == MaterialParamType::Mat3) ? 9 : 16;
                bool changed = false;
                if(ImGui::TreeNode("矩阵(列主序)"))
                {
                    for(uint32_t i = 0; i < element_count; ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));
                        if(ImGui::DragFloat("##m", &v[i], 0.01f))
                        {
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                return changed;
            }

            default:
                ImGui::TextDisabled("(invalid)");
                return false;
        }
    }
} // namespace ID
