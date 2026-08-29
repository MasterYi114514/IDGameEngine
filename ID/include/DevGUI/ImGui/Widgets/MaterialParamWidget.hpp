#pragma once

#include "IDpch.hpp"
#include "imgui.h"
#include "Renderer/Material/MaterialParam.hpp"

namespace ID
{
    // 类型徽章短文本（"Float" / "Vec3" ...，None 返回 "None"）
    const char* ID_API material_param_type_tag(MaterialParamType type);

    /*
    *   绘制单个材质参数控件，直接编辑 param.value 的 float 数组。
    *   返回本帧是否有修改（调用方据此写回 Material / MaterialInstance）。
    *   调用方需先 PushID 保证控件 ID 唯一。函数体较长，实现位于 .cpp。
    *   本组件为纯控件，不含任何业务逻辑（不访问 Material / MaterialInstance）。
    */
    bool ID_API draw_material_param_editor(MaterialParam& param, bool is_color);
} // namespace ID
