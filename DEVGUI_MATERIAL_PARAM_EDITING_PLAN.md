# DevGUI 材质参数编辑支持 — 实施计划书

> **目标**：使 DevGUI 支持编辑 `Material` 的**任意类型**默认参数（Int / Float / Vec2 / Vec3 / Vec4 / Mat3 / Mat4）
> 与 `MaterialInstance` 的参数 Override，替代当前仅对 `u_color` 的硬编码支持。
>
> **本计划的执行者**：AI 编码助手。**必须**遵守本文档第 0 节的执行协议。

---

## 0. 执行协议（实施 AI 必读）

1. **开工前必读**（按顺序，读完才允许写代码）：
   - `C:/Users/21059/.pi/agent/skills/yy-coding/SKILL.md`（通用 C++ 规范：头文件 ≤3 行函数体等）
   - `C:/Users/21059/.pi/agent/skills/id-coding-style/SKILL.md`（命名空间 / 宏前缀 / include 路径规范）
   - `D:/IDGameEngine/.pi/skills/id-renderer-design/SKILL.md`（IDRenderer 架构铁律：#ifdef 整类、平台头文件隔离、检查清单）
2. **检查点机制**：本文档每隔若干步骤设置一个 `⏸ CHECKPOINT`。**执行到检查点时必须暂停编码**，
   重新完整阅读上述三个 Skill 与**本计划书全文**，确认进度与计划一致后再继续。这是为了对抗上下文遗忘。
3. **每完成一个 Step**：对照该 Step 的「验收」自检；不通过则修复后再进入下一步。
4. **编码铁律摘要**（详见 Skill 原文）：
   - 非模板函数体 >3 行禁止放头文件；
   - `#include` 一律以 `include/` 或 `src/` 为根，禁止 `../`；
   - `#ifdef` 包裹**整个**代码块，两个实现时 `#else` 必须存在；
   - 所有成员变量必须有默认值；
   - 公开头文件（`include/`）**禁止**出现 glad/vulkan 等平台头文件；
   - IDRenderer 公开符号用 `IDR_API`，ID 引擎层用 `ID_API`；
   - **做外科手术式修改**：不顺手重构、不扩大改动范围、不修改与本计划无关的代码。

---

## 1. 现状梳理（已核对的代码逻辑）

### 1.1 Core/IDArray.hpp
`ID::Array<T, SIZE>`：编译期固定大小数组，memcpy 拷贝。`MaterialParam` 用 `Array<float, 16>` 存储
uniform 值（最大 Mat4 = 16 floats），是参数值数据的物理载体。

### 1.2 Material 体系（ID/include/Renderer/Material/，实现于 ID/src/Renderer/Material/）
- **MaterialParam**：`{ MaterialParamType type; Array<float,16> value; }`。模板构造 `MaterialParam(T)`
  按 `MPSupported` 概念从 `float/int/Vec2/Vec3/Vec4/Mat3/Mat4` 构造（int 存为 float，应用时转回）。
  序列化格式 `{"type":"Vec3","value":[...]}`。
- **Material**：`ShaderID + name + map<string,MaterialParam> m_param_defaults +
  map<string,TextureBindingDesc> m_texture_defaults + m_transparent`。
  `set_param<T>()`（模板）写默认值；`apply()` 遍历全部 defaults，经静态 `apply_param()` →
  `IDRCmd::set_param` 写入管线（按 type switch 分发）；纹理绑定写 int 槽位 + `bind_texture`。
- **MaterialInstance**：`const Material* m_parent` + 参数/纹理两张 **override** 表。
  `apply()` 先应用父级 defaults 再应用 overrides；`clear_override(name) / clear_all_overrides()`；
  `set_param_value(name, MaterialParam)` 可直接写入 override 表（**Material 没有对应的非模板接口，需补充**）。
- **MaterialLibrary**：静态类，`vector<unique_ptr<Material>>` 存储，`add/get/get_all/remove` +
  全量序列化（场景 Save/Load 自动同步 `Assets/material/`）。

### 1.3 DevGUI 两个 Panel
- **AssetPanel::render_material_section()**（AssetPanel.cpp ~259-425 行）：
  创建材质（选 shader + 名字）→ 材质库列表（BulletText + 右键重命名 + 删除，删除前检查场景引用）→
  **硬编码的 `u_color` ColorEdit3 第二行**（材质缺 `u_color` 时还会自动补白色）→ 材质库文件 Save/Load。
- **InspectorPanel::render_mesh_renderer_editor()**（InspectorPanel.cpp ~280-490 行）：
  Mesh 选择/子网格切换/拖拽 → Material Combo（枚举 MaterialLibrary，选中即
  `model.set_material(MaterialInstance(*mat))`）→ 拖拽区（shader 重建材质【也硬编码补 `u_color`】/
  纹理绑定到首个 texture binding）。

### 1.4 关键缺口（本计划要解决的根因）
1. **无 shader uniform 反射**：`IDLib/IDRenderer` 的 `Shader` 类只有 `get_uniform_location()`（按名查询），
   无法枚举 shader 有哪些 uniform、什么类型 → DevGUI 只能硬编码 `u_color`。
2. `Material` 缺少 `set_param(name, const MaterialParam&)` 非模板重载，难以把反射默认值写入材质。
3. `MaterialInstance` 完全没有参数 Override 编辑 UI。

### 1.5 引擎保留 uniform（材质**不可**编辑，由各 RenderPass 驱动）
以下名单从 `src/Renderer/Render/` 与 `src/Renderer/Shadow/` 的 `set_param` 调用扫描得出（Phase B 固化进代码）：
`u_mvp, u_model, u_view, u_proj, u_projection, u_camera_pos, u_time, u_ambient, u_light_count,
u_light_dirs, u_light_positions, u_light_colors, u_light_space_mvp, u_light_view_proj,
u_shadow_enabled, u_shadow_map, u_shadow_bias, u_shadow_pcf_radius, u_normal_bias, u_texel_size,
u_sun_dir, u_sun_intensity, u_use_cubemap, u_cubemap, u_top_color, u_horizon_color, u_bottom_color,
u_input, u_mode, u_tone_mapping, u_gamma, u_threshold, u_bloom, u_bloom_strength, u_has_bloom`

---

## 2. 目标 / 非目标

**目标**
1. IDRenderer：Shader 构造时用 GL 反射枚举 active uniforms（后端无关描述符）。
2. ID 层：暴露反射 + 过滤保留名单 + 类型映射 + 默认值生成的工具集。
3. DevGUI/AssetPanel：材质条目可展开「参数」子区，编辑/增删**任意类型**默认参数。
4. DevGUI/InspectorPanel：MeshRenderer 的材质可编辑 Override，可逐项/全部重置。
5. 全程兼容现有序列化（Material / MaterialInstance / 材质库文件 / 场景文件）。

**非目标**
- 不做纹理绑定 UI 重构（沿用现有拖拽绑定逻辑，sampler 类型在参数列表中隐藏）。
- 不做数组 uniform、Bool uniform 的编辑（反射时跳过，UI 不展示）。
- 不引入参数元数据系统（min/max/range/注释），仅用「名字含 color → 颜色控件」启发式。

---

## 3. 总体架构

```
IDLib/IDRenderer（后端反射）                ID 引擎层（包装 + 过滤）           DevGUI（UI）
┌──────────────────────────┐   ┌──────────────────────────────────┐   ┌─────────────────────┐
│ Shader 构造时枚举         │   │ ID::ShaderManager::               │   │ MaterialParamWidget │
│ glGetActiveUniform        │ → │   get_active_uniforms()          │ → │  (通用控件绘制)      │
│ → vector<ShaderUniform-  │   │ MaterialReflection:               │   │ AssetPanel 材质参数  │
│    Desc>（含类型映射/过滤）│   │   get_editable_params()          │   │ Inspector Override  │
│ ResourceManager 暴露      │   │   is_engine_reserved_uniform()   │   │                     │
│   get_active_uniforms()   │   │   make_default_param()           │   │                     │
└──────────────────────────┘   └──────────────────────────────────┘   └─────────────────────┘
```

数据流：**反射数据一次性生成**（Shader link 后），UI 每帧取用（列表很小，拷贝可接受）。
编辑写回：UI 控件直接改 `MaterialParam::value` 的 float 数组 → 修改后经
`Material::set_param` / `MaterialInstance::set_param_value` 写入 → 渲染时 `apply()` 生效。

---

## 4. 分步实施流程

> 全局约定：新增 `.cpp` 均被各自 CMake 的 `GLOB_RECURSE CONFIGURE_DEPENDS` 自动纳入（需重跑 cmake 配置）。
> 以下文件路径根：`IDLib/IDRenderer/` 记作 **[IDR]**，`ID/` 记作 **[ID]**。

### Phase A — IDRenderer：Shader uniform 反射

**[x] Step A1：新增公开描述符 `ShaderUniformDesc`**
- 新建 `[IDR]include/Resource/Shader/ShaderUniformDesc.hpp`：
```cpp
#pragma once

#include "Core/IDRpch.hpp"

namespace ID
{
    // 后端无关的 uniform 类型抽象（公开头文件，禁止依赖 glad / vulkan）
    enum class ShaderUniformType : uint8_t
    {
        None = 0, Float, Int, Bool, Vec2, Vec3, Vec4, Mat3, Mat4,
        Sampler2D, SamplerCube, Unsupported
    };

    // 单个 active uniform 的反射描述
    struct IDR_API ShaderUniformDesc
    {
        std::string         name;                              // 数组已截断 "[0]" 的基础名
        ShaderUniformType   type = ShaderUniformType::None;
        uint32_t            count = 1;                         // >1 表示数组
    };
} // namespace ID
```
- 验收：仅依赖 `IDRpch.hpp`（标准库聚合）；成员有默认值；`IDR_API` 导出。

**[x] Step A2：`Shader` 类构造时枚举 active uniforms**
- 改 `[IDR]src/Resource/Shader/Shader.hpp`（在已有 `#ifdef IDRENDERER_USE_OPENGL` 的类内）：
  - 新增成员 `std::vector<ShaderUniformDesc> m_active_uniforms;`（include `Resource/Shader/ShaderUniformDesc.hpp`）
  - 新增 getter `const std::vector<ShaderUniformDesc>& get_active_uniforms() const { return m_active_uniforms; }`（≤3 行，可留头文件）
- 改 `[IDR]src/Resource/Shader/Shader.cpp`（同样在 `#ifdef IDRENDERER_USE_OPENGL` 内）：
  - 匿名命名空间新增两个 helper：
    - `ShaderUniformType gl_type_to_uniform_type(GLenum)`：switch 映射 `GL_FLOAT/GL_INT/GL_BOOL/
      GL_FLOAT_VEC2/GL_FLOAT_VEC3/GL_FLOAT_VEC4/GL_FLOAT_MAT3/GL_FLOAT_MAT4/GL_SAMPLER_2D/
      GL_SAMPLER_CUBE`，default → `Unsupported`；
    - `std::vector<ShaderUniformDesc> collect_active_uniforms(GLuint program)`：
      `glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &n)` → 循环 `glGetActiveUniform(program, i, 256, ...)`；
      **跳过**：名字含 `.`（UBO block 成员）、以 `gl_` 开头（内建保险）、映射后 `Unsupported` 的数组 `count > 1` 的照常记录 count；
      名字截断 `find('[')` 去掉 `[0]` 后缀；结果按 `name` 字典序 `std::sort`（保证 UI 顺序稳定）。
  - 构造函数末尾（`attach_and_link` 之后）：`m_active_uniforms = collect_active_uniforms(m_program_id);`
  - 移动构造 / 移动赋值：把 `m_active_uniforms` 与现有成员一起 move / swap；
    `destroy()`：清空 `m_active_uniforms`（与现有 `m_uniform_location_cache.clear()` 并列）。
- 验收：临时加 `IDR_INFO` 打印（验证后**删除**），运行 Sandbox 应看到 geometry shader 反射出
  `texture_sampler / u_color / u_camera_pos / u_ambient / u_light_* / u_shadow_*` 等；
  `u_light_dirs` 等数组以基础名 + count=8 出现。
- ⚠ 遵守 id-renderer-design：glad 相关代码只出现在 `src/`；成员有默认值；移动语义转移所有权。

**[x] Step A3：`ResourceManager` 暴露反射查询接口**
- 改 `[IDR]include/Resource/ResourceManager.hpp`：
  - 顶部 include `Resource/Shader/ShaderUniformDesc.hpp`（签名用到返回类型）；
  - Shader 接口段新增声明（与 `get_vertex_count` 同款风格）：
```cpp
static std::vector<ShaderUniformDesc> get_active_uniforms(const ShaderID& id)
    requires ShaderRes<T, ResType>;
```
- 改 `[IDR]src/Resource/ResourceManager.cpp`：新增模板特化实现——经匿名命名空间
  `ResourceGetter::get_shader(id)` 取指针，判空（无效 ID 打 `IDR_ERROR` 返回空表），
  返回 `shader->get_active_uniforms()` 的拷贝。
- 验收：对照 id-renderer-design「新增功能检查清单」逐项核对（声明/特化/命名/导出宏）。

**[x] Step A4：伞形头文件聚合 + 编译验证**
- 改 `[IDR]include/IDRenderer.hpp`：确认/补充 `#include "Resource/Shader/ShaderUniformDesc.hpp"`。
- 编译 IDRenderer（Debug 即可），零错误零新警告。删除所有临时调试打印。
- 验收：`cmake --build build --target IDRenderer` 通过。

> ### ⏸ CHECKPOINT 1（Phase A 完成后）
> 1. 重新完整阅读：`yy-coding` → `id-coding-style` → `id-renderer-design` 三个 SKILL.md。
> 2. 重新完整阅读**本计划书全文**（从第 0 节开始）。
> 3. 确认进度：A1~A4 已完成，反射数据链路（Shader 内部 → ResourceManager）已打通；下一步进入 Phase B。
> 4. 若已写代码与计划冲突：先修正再继续；不得凭记忆继续编码。

### Phase B — ID 引擎层：反射包装与过滤工具

**[x] Step B1：`ID::ShaderManager` 暴露反射**
- 改 `[ID]include/Renderer/Resource/ShaderManager.hpp`：新增声明
  `static std::vector<ShaderUniformDesc> get_active_uniforms(ShaderID shader_id);`
  （需要 include `Renderer/IDRCore.hpp` 已有；`ShaderUniformDesc` 经 IDRenderer.hpp 可见，若不可见则显式 include IDRenderer 伞形头）。
- 改 `[ID]src/Renderer/Resource/ShaderManager.cpp`：实现为调用全局 IDRenderer 管理器
  `return ::ShaderManager::get_active_uniforms(shader_id);`（注意 `::` 全局限定——ID 层 `ID::ShaderManager`
  遮蔽了全局别名，现有 `create()` 已是此模式，照抄风格）。
- 验收：编译通过；对无效 ShaderID 返回空表（由 A3 的判空保证）。

**[x] Step B2：新增 `MaterialReflection` 工具集（本计划核心枢纽）**
- 新建 `[ID]include/Renderer/Material/MaterialReflection.hpp`：
```cpp
#pragma once

#include "IDpch.hpp"
#include "Renderer/Material/MaterialParam.hpp"

namespace ID
{
    // 材质可编辑参数的描述（已完成保留名单过滤与类型映射）
    struct ID_API EditableParamDesc
    {
        std::string         name;
        MaterialParamType   type = MaterialParamType::None;
        bool                is_color = false;   // true 时 UI 用 ColorEdit 呈现
    };

    // 引擎保留 uniform 判断（RenderPass/Shadow 驱动的帧级/物体级 uniform，材质不可编辑）
    bool ID_API is_engine_reserved_uniform(const std::string& name);

    // 名字启发式判断颜色参数（含 "color"/"colour"/"tint"，不区分大小写）
    bool ID_API looks_like_color_name(const std::string& name);

    // 反射 + 过滤 + 映射：返回材质可编辑参数列表（按名称排序）
    // 过滤规则：保留名单 / 数组(count>1) / Sampler / Bool / Unsupported / 类型映射失败
    std::vector<EditableParamDesc> ID_API get_editable_params(ShaderID shader);

    // 类型默认参数：颜色 → 全 1（白），其余 → 全 0
    MaterialParam ID_API make_default_param(const EditableParamDesc& desc);
} // namespace ID
```
- 新建 `[ID]src/Renderer/Material/MaterialReflection.cpp`：
  - 匿名命名空间定义保留名单（第 1.5 节的 35 个名字的 `static const std::unordered_set<std::string>` 或数组）；
  - `get_editable_params`：`ShaderManager::get_active_uniforms()` → 逐条过滤 →
    `ShaderUniformType → MaterialParamType` 映射（Float/Int/Vec2/Vec3/Vec4/Mat3/Mat4 直映射；
    Sampler2D/SamplerCube/Bool/Unsupported 跳过；数组 count>1 跳过）→
    `is_color = (Vec3||Vec4) && looks_like_color_name(name)`；
  - `make_default_param`：构造对应类型 `MaterialParam`（switch 写各类型默认值；颜色 Vec3→(1,1,1)、Vec4→(1,1,1,1)）。
- 验收：编译通过；对 geometry shader 调用应只剩 `u_color`（is_color=true）；对 skybox shader 应返回空表（全部被保留名单滤掉）。

**[x] Step B3：`Material` 补充非模板写参接口**
- 改 `[ID]include/Renderer/Material/Material.hpp`：与模板 `set_param` 并列新增
```cpp
// 非模板重载：直接写入构造好的 MaterialParam（供反射默认值填充 / DevGUI 写回使用）
void set_param(const std::string& name, const MaterialParam& param)
{
    m_param_defaults[name] = param;
}
```
（≤3 行，允许留在头文件；与模板重载不冲突。）
- 验收：编译通过；现有 `set_param<T>` 调用点不受影响。

> ### ⏸ CHECKPOINT 2（Phase B 完成后）
> 1. 重新完整阅读三个 SKILL.md 与**本计划书全文**。
> 2. 确认进度：B1~B3 完成；`get_editable_params(shader)` 已可在 ID 层任意处调用并得到正确过滤结果；下一步进入 Phase C。
> 3. 特别回忆：yy-coding 的「头文件 ≤3 行函数体」、id-coding-style 的「include 根路径」、id-renderer-design 的「公开头文件无平台依赖」。
> 4. 检查自己是否顺手改了任何与本计划无关的代码——有则回退。

### Phase C — DevGUI：通用参数控件组件

**[x] Step C1：新建 `MaterialParamWidget`**
- 新建 `[ID]include/DevGUI/ImGui/Widgets/MaterialParamWidget.hpp`：
```cpp
#pragma once

#include "IDpch.hpp"
#include "imgui.h"
#include "Renderer/Material/MaterialParam.hpp"

namespace ID
{
    // 类型徽章短文本（"Float" / "Vec3" ...，None 返回 "None"）
    const char* ID_API material_param_type_tag(MaterialParamType type);

    // 绘制单个材质参数控件，直接编辑 param.value 的 float 数组。
    // 返回本帧是否有修改（调用方据此写回 Material / MaterialInstance）。
    // 调用方需先 PushID 保证控件 ID 唯一。函数体较长，实现位于 .cpp。
    bool ID_API draw_material_param_editor(MaterialParam& param, bool is_color);
} // namespace ID
```
- 新建 `[ID]src/DevGUI/ImGui/Widgets/MaterialParamWidget.cpp`，实现要点：
  - `switch(param.type)` 分发控件：
    `Float → DragFloat("##v", &v[0], 0.01f)`；`Int → InputInt`（局部 int 缓存，改动写回 `value[0]`）；
    `Vec2 → DragFloat2`；`Vec3 → is_color ? ColorEdit3 : DragFloat3`；
    `Vec4 → is_color ? ColorEdit4 : DragFloat4`（速度统一 `0.01f`）；
    `Mat3 / Mat4 → TreeNode("矩阵(列主序)")` 内 PushID(i) 逐元素 `DragFloat`（9 / 16 个）；
    `None → TextDisabled("(invalid)")` 返回 false。
  - 控件返回值用 `||` 累积成函数返回值。
  - 注释说明：矩阵按 GL 列主序内存顺序直接展示（与 `IDMath::Mat3/Mat4::get_data()` 一致）。
- 验收：编译通过；本组件**不含**任何业务逻辑（不访问 Material/MaterialInstance），可独立测试。

> ### ⏸ CHECKPOINT 3（Phase C 完成后）
> 1. 重新完整阅读三个 SKILL.md 与**本计划书全文**。
> 2. 确认进度：C1 完成；通用控件就绪；下一步 Phase D 将把它接入 AssetPanel。
> 3. 回忆计划书第 1.3 节 AssetPanel 现状与第 1.5 节保留名单，D 阶段要用。

### Phase D — AssetPanel：Material 默认参数编辑

**[x] Step D1：材质条目接入参数编辑 TreeNode**
- 改 `[ID]src/DevGUI/ImGui/Panels/AssetPanel.cpp` 的 `render_material_section()`：
  - include `Renderer/Material/MaterialReflection.hpp`、`DevGUI/ImGui/Widgets/MaterialParamWidget.hpp`；
  - **删除**原「第二行：u_color 颜色编辑」整块（约 368~390 行，含缺省补白色逻辑）；
  - 在材质条目（BulletText + 删除按钮行）之后新增：
```cpp
// 参数编辑子区（默认收起，避免材质列表过长）
if(ImGui::TreeNode(("参数##mat_params_" + mat->get_name()).c_str()))
{
    const std::vector<Material*> mats_snapshot …… // 注意：mat 指针本帧有效，无需快照
    const std::vector<EditableParamDesc> editable = MaterialReflection::get_editable_params(mat->get_shader());
    for(const EditableParamDesc& desc : editable)
    {
        ImGui::PushID(desc.name.c_str());
        // 当前值：材质已有该参数 → 取之；没有 → 类型默认值（仅显示，改动才写入材质）
        MaterialParam param = mat->has_param(desc.name)
            ? mat->get_param_defaults().at(desc.name)
            : MaterialReflection::make_default_param(desc);
        param.type = desc.type;    // 类型以 shader 反射为准，纠正旧数据类型漂移
        ImGui::TextUnformatted(desc.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", material_param_type_tag(desc.type));
        ImGui::SameLine();
        if(draw_material_param_editor(param, desc.is_color))
        {
            mat->set_param(desc.name, param);   // B3 的非模板重载
        }
        ImGui::PopID();
    }
    // 材质中存在但 shader 已不存在的陈旧参数：警告 + 移除按钮
    for(const auto& [pname, pparam] : mat->get_param_defaults())
    {
        if(pparam.is_valid() && !shader 有 pname /* 用 editable 列表判断 */)
        {
            ImGui::TextColored(黄, "%s (shader 中已不存在)", pname);
            ImGui::SameLine();
            if(ImGui::SmallButton(("移除##stale_" + pname).c_str())) mat->remove_param(pname);
        }
    }
    ImGui::TreePop();
}
```
  （上面为伪代码级骨架，落地时按实际变量名与代码风格整理；「shader 有 pname」用一个 `std::set<std::string>` 查询。）
- 验收：展开材质参数区能看到 `u_color` 颜色控件（等价旧行为）；编辑后渲染即时变化；
  **未改动时不写材质**（断点/日志确认 `set_param` 仅在控件返回 true 时调用）。

**[x] Step D2：创建材质时按反射填充默认参数，删除 u_color 硬编码**
- 同文件 Create Material 按钮逻辑：删除 `mat->set_param("u_color", Vec3(1,1,1));`，替换为：
```cpp
if(is_new)
{
    for(const EditableParamDesc& desc : MaterialReflection::get_editable_params(shader_id))
    {
        mat->set_param(desc.name, MaterialReflection::make_default_param(desc));
    }
}
```
- 验收：新建 geometry 材质自动带 `u_color=(1,1,1)`；新建其它 shader 材质按其 uniform 自动填充。

**[x] Step D3：参数行右键「移除参数」**
- D1 的参数行内加 `BeginPopupContextItem` 右键菜单：`MenuItem("移除参数")` → `mat->remove_param(desc.name)`。
  （风格参照同文件已有的右键重命名菜单，注意显式 str_id。）
- 验收：移除后参数从材质 defaults 消失（TreeNode 收起再展开不复活）；材质库 Save → JSON 中该参数消失。

> ### ⏸ CHECKPOINT 4（Phase D 完成后）
> 1. 重新完整阅读三个 SKILL.md 与**本计划书全文**。
> 2. 确认进度：D1~D3 完成；AssetPanel 已无任何 `u_color` 硬编码（全局 grep `u_color` 确认，AssetPanel 内应为 0 处）；下一步 Phase E。
> 3. 回忆 InspectorPanel 现状（计划书 1.3 节第二段）与 `MaterialInstance` 的 override 语义（1.2 节）。
> 4. 检查点自检：材质删除循环中的悬垂指针保护（删除后 `break`）是否仍完好。

### Phase E — InspectorPanel：MaterialInstance Override 编辑

**[x] Step E1：材质参数 Override 编辑区**
- 改 `[ID]src/DevGUI/ImGui/Panels/InspectorPanel.cpp` 的 `render_mesh_renderer_editor()`：
  - include 同 D1 两个新头文件；
  - 在 Material Combo（`ImGui::EndCombo()` 的 Combo 块）之后、拖拽区之前插入：
```cpp
// ---- Material 参数 Override 编辑（材质有效时）----
MaterialInstance& inst = model.get_material();   // 注意：用非 const 引用（已有此 API）
if(const Material* parent = inst.get_parent())
{
    if(ImGui::TreeNode("材质参数 (Override)"))
    {
        const auto& overrides  = inst.get_param_overrides();
        const auto& defaults   = parent->get_param_defaults();
        for(const EditableParamDesc& desc : MaterialReflection::get_editable_params(parent->get_shader()))
        {
            ImGui::PushID(desc.name.c_str());
            // 显示值优先级：override > 父默认 > 类型默认
            auto oit = overrides.find(desc.name);
            auto dit = defaults.find(desc.name);
            MaterialParam param = (oit != overrides.end()) ? oit->second
                : (dit != defaults.end()) ? dit->second
                : MaterialReflection::make_default_param(desc);
            param.type = desc.type;                     // 类型以 shader 反射为准
            const bool overridden = (oit != overrides.end());
            if(overridden) ImGui::TextUnformatted("[*] ");  // override 徽标
            else           ImGui::TextUnformatted("    ");
            ImGui::SameLine();
            ImGui::TextUnformatted(desc.name.c_str());
            ImGui::SameLine();
            if(draw_material_param_editor(param, desc.is_color))
            {
                inst.set_param_value(desc.name, param);  // 写入 override
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}
```
- 验收：选中带材质的物体 → 展开 → 修改值 → 渲染变化且 `[*]` 徽标出现；父材质默认值同步显示。

**[x] Step E2：Override 重置**
- E1 的参数行：`overridden` 时在行尾加 `SmallButton("重置")` → `inst.clear_override(desc.name)`
  （注意 `clear_override` 会连带清同名 texture override，本计划 uniform 名与 sampler 名不重叠，可接受——加注释说明）。
- TreeNode 标题行旁（TreePop 之前底部）加 `Button("Clear All Overrides")` → `inst.clear_all_overrides()`。
- 验收：重置后徽标消失、值回到父默认；Clear All 清空全部 override。

**[x] Step E3：清理 InspectorPanel 残留的 u_color 硬编码**
- 拖拽 shader 重建材质处（~460 行）：删除 `mat->set_param("u_color", ...)`，替换为 D2 同款反射填充循环
  （建议将「按反射填充默认参数」提为该文件匿名命名空间的 helper，两处复用时再决定是否上移到 MaterialReflection——
  本计划允许就地复制一次，保持外科手术式改动）。
- 验收：全局 grep `"u_color"`：ID/ 目录源码中应仅剩注释或零处（ShaderManager 无关）。

> ### ⏸ CHECKPOINT 5（Phase E 完成后）
> 1. 重新完整阅读三个 SKILL.md 与**本计划书全文**。
> 2. 确认进度：E1~E3 完成；功能开发全部结束；下一步 Phase F 集成验证。
> 3. 回忆 yy-coding 的 Agent 规范：只做本计划内修改；发现的其它问题记录、不顺手修。

### Phase F — 集成验证与文档更新

**[x] Step F1：全量编译**（用户执行，通过）
- 完整构建整个解决方案（IDRenderer → ID → Sandbox），零错误、零新警告。

**Step F2：Sandbox 手动验证清单（逐项打勾）**（⏳ 待用户运行 Sandbox 逐项验证）
1. AssetPanel 创建 geometry 材质 → 参数区显示 `u_color` 颜色控件，默认白色；
2. 修改颜色 → 视口渲染即时变化；保存材质库 → JSON 含正确 type/value；重新 Load Library → 值保留；
3. 拖一个非 geometry shader 建材质 → 参数区只显示该 shader 的可编辑参数，引擎保留项**不出现**；
4. Inspector 选中物体 → 材质参数 Override 编辑 → `[*]` 徽标 → 修改生效；重置 → 回父默认；Clear All 生效；
5. 保存场景 → 重新加载场景 → 材质默认值与 override 均保留（MaterialInstance 序列化存 parent_name + overrides）；
6. 旧材质库文件（无新参数）加载 → 不崩溃，参数区显示类型默认值，改动后写入；
7. 删除正被引用的材质 → 仍被拒绝（悬垂保护未破坏）。

**[x] Step F3：更新架构文档**
- 按 `.pi/agent/AGENTS.md` 要求，落地后更新 `D:/IDGameEngine/.pi/agent/ID_ENGINE_ARCHITECTURE_PLAN.md`：
  在 Material 章节补充 Shader uniform 反射链路与 DevGUI 参数编辑能力（标注 ✅）。
- 在本计划书每个已完成 Step 前打 `[x]`，作为完成记录。

> ### ⏸ CHECKPOINT 6（最终，Phase F 完成后）
> 1. 最后一次完整重读三个 SKILL.md 与本计划书全文。
> 2. 核对第 5 节「风险与注意事项」逐条确认已规避；核对第 6 节总验收清单全部打勾。
> 3. 确认无遗留 TODO / 调试打印 / 被注释掉的旧代码块；git diff 只包含本计划涉及的文件。

---

## 5. 风险与注意事项（实施全程对照）

| # | 风险 | 规避方式 |
|---|------|----------|
| 1 | GL 只反射 **active** uniform，被编译器剔除的 uniform 不会出现 | 材质 defaults 里多余参数 → apply 时 `set_param` 是合法 no-op；UI 显示「shader 中已不存在」警告（D1） |
| 2 | 数组 uniform（`u_light_dirs[8]`）名字带 `[0]`、count>1 | A2 截断基础名 + 记录 count；B2 过滤 count>1 |
| 3 | UO block 成员混入反射 | A2 过滤名字含 `.` 的条目 |
| 4 | override / defaults 类型与 shader 漂移（旧序列化数据） | UI 一律以反射类型渲染（`param.type = desc.type`），写回即纠正 |
| 5 | 每帧调用 `get_editable_params` 产生分配 | 列表极小（<10 项）；反射本体在 Shader 构造时一次性完成。若后续 profiling 有压力再加缓存（本计划不做） |
| 6 | ImGui 控件 ID 冲突 | 所有参数行 `PushID(name)`，TreeNode/按钮带材质名后缀（沿用现有 `##xxx_` 模式） |
| 7 | 材质删除后 `mat` 悬垂 | 沿用现有「先拷名、检查引用、删后 break」模式，禁止改动该保护逻辑 |
| 8 | `clear_override` 连带清同名 texture override | uniform 名与 sampler 名约定不重叠；调用处加注释说明 |
| 9 | 新增 pass uniform 忘记加入保留名单 → 泄入材质编辑 UI | 名单集中于 MaterialReflection.cpp 一处，文件头注释注明维护规则 |
| 10 | 头文件内长函数体 / 平台头文件泄漏 | CHECKPOINT 反复重读 yy-coding / id-renderer-design 核对 |

---

## 6. 总验收清单

> 代码层面可静态确认的项已打勾；标注（⏳）的项需 Sandbox 运行时验证。

- [x] `ShaderUniformDesc` / `ShaderUniformType` 落地且公开头文件零平台依赖；
- [x] `ID::ShaderManager::get_active_uniforms()` 可用；
- [x] `MaterialReflection::get_editable_params()` 正确过滤保留名单 / 数组 / sampler
  （静态核对：geometry.fsl 反射后仅剩 `u_color`，skybox.fsl 全被保留名单滤掉）；
- [x] `Material` 具备非模板 `set_param(name, MaterialParam)`；
- [x] `draw_material_param_editor` 支持全部 7 种类型控件（颜色启发式生效）；
- [x] AssetPanel：材质参数 TreeNode 可编辑 / 移除；创建材质自动填充默认参数；`u_color` 硬编码清零
  （grep 确认 AssetPanel / InspectorPanel 源码 `u_color` 均为 0 处）；
- [x] InspectorPanel：Override 编辑 + 徽标 + 单项重置 + 全部重置；拖拽建材质走反射填充；
- [ ]（⏳）序列化 roundtrip（材质库文件 / 场景文件）全部通过（F2 第 2、5 项）；
- [ ]（⏳）旧数据兼容（F2 第 6 项）与悬垂保护（F2 第 7 项，代码层面已确认未改动保护逻辑）；
- [x] 架构计划书已更新，本计划书已打勾归档。

---

## 7. 涉及文件总览

| 操作 | 文件 |
|------|------|
| 新建 | `[IDR]include/Resource/Shader/ShaderUniformDesc.hpp` |
| 修改 | `[IDR]src/Resource/Shader/Shader.hpp` / `.cpp` |
| 修改 | `[IDR]include/Resource/ResourceManager.hpp` / `[IDR]src/Resource/ResourceManager.cpp` |
| 修改 | `[IDR]include/IDRenderer.hpp` |
| 修改 | `[ID]include/Renderer/Resource/ShaderManager.hpp` / `[ID]src/Renderer/Resource/ShaderManager.cpp` |
| 新建 | `[ID]include/Renderer/Material/MaterialReflection.hpp` / `[ID]src/Renderer/Material/MaterialReflection.cpp` |
| 修改 | `[ID]include/Renderer/Material/Material.hpp` |
| 新建 | `[ID]include/DevGUI/ImGui/Widgets/MaterialParamWidget.hpp` / `[ID]src/DevGUI/ImGui/Widgets/MaterialParamWidget.cpp` |
| 修改 | `[ID]src/DevGUI/ImGui/Panels/AssetPanel.cpp` |
| 修改 | `[ID]src/DevGUI/ImGui/Panels/InspectorPanel.cpp` |
| 更新 | `.pi/agent/ID_ENGINE_ARCHITECTURE_PLAN.md`（F3） |
