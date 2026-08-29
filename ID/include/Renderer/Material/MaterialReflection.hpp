#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Material/MaterialParam.hpp"

namespace ID
{
    /*
    *   材质可编辑参数的描述（已完成保留名单过滤与类型映射）
    */
    struct ID_API EditableParamDesc
    {
        std::string         name;
        MaterialParamType   type = MaterialParamType::None;
        bool                is_color = false;   // true 时 UI 用 ColorEdit 呈现
    };

    /*
    *   MaterialReflection — Shader uniform 反射结果到材质可编辑参数的包装工具集
    *
    *   数据流：ShaderManager::get_active_uniforms()（Shader link 时一次性反射）
    *     → get_editable_params() 过滤（保留名单 / 数组 / Sampler / Bool / Unsupported）
    *     → DevGUI 据此渲染任意类型参数控件，并经 make_default_param() 提供初值
    */
    // 引擎保留 uniform 判断（RenderPass/Shadow 驱动的帧级/物体级 uniform，材质不可编辑）
    bool ID_API is_engine_reserved_uniform(const std::string& name);

    // 名字启发式判断颜色参数（含 "color"/"colour"/"tint"，不区分大小写）
    bool ID_API looks_like_color_name(const std::string& name);

    // 反射 + 过滤 + 映射：返回材质可编辑参数列表（按名称排序，来自反射侧的排序）
    // 过滤规则：保留名单 / 数组(count>1) / Sampler / Bool / Unsupported / 类型映射失败
    std::vector<EditableParamDesc> ID_API get_editable_params(ShaderID shader);

    // 类型默认参数：颜色 → 全 1（白），其余 → 全 0
    MaterialParam ID_API make_default_param(const EditableParamDesc& desc);
} // namespace ID
