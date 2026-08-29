# 延迟渲染支持 计划书

> 本计划书把「ID 引擎支持延迟渲染路径」的完整设计方案，拆解为若干可独立验收的实施步骤。
> **执行本计划书的 AI 必须严格遵守第 2 节「执行纪律」。**
>
> 本计划书风格与先例 `.pi/RenderGraphDAG_TaskBook.md` 一致，可作为格式参照。

---

## 1. 背景与目标

### 1.1 现状（前向渲染路径）

当前引擎唯一渲染路径为前向渲染（Forward）：

```
ShadowPass → ForwardPass（几何 + 逐光源光照，geometry.fsl）
           → SkyboxPass → TransparentPass → PostProcessPass
```

前向路径的固有瓶颈：

- **光照成本 = O(不透明物体像素 × 光源数)**：每个 draw call 的 fragment shader 都要循环全部光源（`geometry.fsl` 中 `u_light_dirs[8]` 等数组），且 **`MAX_LIGHTS = 8` 硬截断**，多光源场景直接丢光。
- 光照计算与材质/几何绘制耦合在同一个 shader 中，无法单独优化光照阶段。

### 1.2 已具备的基础设施

| 设施 | 现状 | 与延迟渲染的差距 |
|------|------|------------------|
| RenderGraph DAG | ✅ 已完成（依赖自动推导 / 拓扑排序 / 死 Pass 剔除） | 缺少 `GBuffer` 语义资源槽位 |
| FrameBuffer（IDRenderer） | 单颜色附件（`m_color_tex`），`get_color_attachment(index)` 预留了 MRT 下标但永远只返回 0 号 | **不支持 MRT**（多渲染目标），G-Buffer 需要至少 3 个颜色附件 |
| RenderCommand | `bind_framebuffer_color(fb, attachment, slot)` 已带附件下标参数 | 缺少深度 blit（延迟模式下 G-Buffer 深度需复制给场景 FBO 供 Skybox/透明物体深度测试） |
| 现有 Pass | Shadow / Forward / Skybox / Transparent / PostProcess | SkyboxPass、TransparentPass `requires_pass<ForwardPass>()` **硬依赖前向 Pass**，延迟装配下会错误地自动补加 ForwardPass |
| Shader 体系 | `.vsl/.fsl` 文件 + `ShaderManager::create(vs_path, fs_path)` | 缺少 G-Buffer 几何 shader 与延迟光照 shader |
| 全屏三角形 | ✅ `FullscreenQuad` 工具（PostProcessPass 在用） | 无 |
| 材质系统 | `MaterialInstance::apply()` 按 uniform 名写入 shader | 无（只要新 shader 使用同名 uniform 即可复用） |

### 1.3 改造目标

1. **新增延迟渲染路径（Deferred）**：
   `Shadow → GBufferPass（MRT 写 G-Buffer）→ LightingPass（全屏光照）→ Skybox → Transparent → PostProcess`
2. **光照成本降为 O(屏幕像素 × 光源数)**，光源上限从 8 提升到 **32**（延迟路径）。
3. **前向路径完整保留**，两条路径可在 RendererSettingsPanel **运行时切换**，画面互为回归基准。
4. **RenderGraph 深度集成**：G-Buffer 是新的语义资源槽位，依赖推导 / 剔除 / 调试视图全部自动生效，不破坏 Phase 4a 的语义槽位设计。
5. **IDRenderer 保持库定位**：MRT 是通用的 FrameBuffer 能力增强，不含任何引擎层概念。

### 1.4 非目标（本期不做）

- **G-Buffer 压缩编码**（八面体法线编码、深度重建世界坐标等带宽优化）——第一版直接存 RGBA16F 世界坐标/法线，正确性优先
- **Tiled / Clustered 延迟光照分块**
- **MSAA 延迟解析**（`FrameBuffer` 的 MSA 分支与 MRT 正交存在，但延迟路径本期不使用 MSAA）
- **透明物体的延迟化**（透明批次始终走前向混合，这是业界通用做法）
- **G-Buffer 瞬态资源分配**（仍由 Renderer 的 `ensure_gbuffer_fb` 显式管理，Phase 4b 瞬态资源系统落地后再迁移）
- **Vulkan / DirectX 后端的 MRT 实现**（`#ifdef` 结构已预留，本期仅实现 OpenGL 分支）

---

## 2. 执行纪律（每个 AI 动手前必读）

1. **每个步骤开始前**，必须先重新阅读：
   - 当前工作目录下的 `.pi/agent/AGENTS.md`（全局规则与任务分派）
   - 全局通用 skill：`yy-coding`（`C:\Users\21059\.pi\agent\skills\yy-coding\SKILL.md` 及其引用的 `cpp-clangformat.md`）
   - 本地补充 skill：`.pi/skills/id-coding-style/SKILL.md`（**特别注意「编译与验证规则」：禁止 AI 自行编译**）
   - 本地补充 skill：`.pi/skills/id-renderer-design/SKILL.md`（ResourcePool / CreateInfo / 空壳模式 / `#ifdef` 后端分离等铁律）
   - **本计划书全文**（防止遗忘设计与验收要求）
2. **禁止自行编译**：不要主动运行 `cmake --build` / `make` / `g++` 等任何编译链接命令，不要删除、覆盖或重建 `bin/` 下的编译产物。编译验证一律由用户执行；需要验证时，**明确请用户运行并等待反馈**。
3. **一次只做一个步骤**：完成当前步骤 → 对照该步「验收清单」自检 → **请用户编译验证** → 用户确认通过后才进入下一步。
4. **不得擅自扩大改动范围**：只改步骤「涉及文件」中列出的文件；遇到设计之外的问题先记录到第 7 节「变更记录」，向用户汇报后再决定。
5. **代码规范底线**（详见 skill，此处为速记）：
   - 头文件中函数体超过三行的实现必须移入 `.cpp`（模板函数例外）；
   - 所有成员变量必须有默认值；
   - `#ifdef` 必须包裹完整代码块（整个类/函数），两个实现时必须有 `#else`；
   - include 路径以 `include/` 或 `src/` 为根，禁止 `../` 相对路径（shader 资产路径 `"../Assets/shader/..."` 是运行时相对路径，不受此限）；
   - 公开类 / 公开函数加 `ID_API`（IDRenderer 内为 `IDR_API`）；日志宏使用 `ID_INFO` / `ID_WARN` / `ID_ERROR`（IDRenderer 内为 `IDR_*`）。

---

## 3. 相关 skill 清单

| 顺序 | skill | 路径 | 适用范围 |
|------|-------|------|----------|
| ① | yy-coding | `C:\Users\21059\.pi\agent\skills\yy-coding\SKILL.md` | 所有步骤 |
| ② | id-coding-style | `.pi/skills/id-coding-style/SKILL.md` | 所有步骤（含编译纪律） |
| ③ | id-renderer-design | `.pi/skills/id-renderer-design/SKILL.md` | Step 1 / 2 / 5（改 IDRenderer 库时） |
| ④ | karpathy-guidelines | `C:\Users\21059\.pi\agent\npm\node_modules\@micka33\pi-karpathy-skill\skills\karpathy-guidelines\SKILL.md` | 动手写代码前快速过一遍（克制改动、外科手术式修改） |

---

## 4. 设计总览（自包含摘要）

### 4.1 总体架构：双渲染路径并存

```
Forward 路径（现状，不动）：
  ShadowPass ──ShadowMap──▶ ForwardPass ──SceneColor──▶ SkyboxPass ──▶ TransparentPass ──▶ PostProcessPass ──▶ ViewportTarget

Deferred 路径（新增）：
  ShadowPass ──ShadowMap────────────────────────────────┐
  GBufferPass ──GBuffer──────────────▶ LightingPass ────┤
                  （读 GBuffer + ShadowMap，写 SceneColor）▼
                                SkyboxPass ──▶ TransparentPass ──▶ PostProcessPass ──▶ ViewportTarget
```

关键点：

- 两条路径**共享** Shadow / Skybox / Transparent / PostProcess 四个 Pass，只有「几何 + 光照」阶段不同（Forward = ForwardPass 一个 Pass；Deferred = GBufferPass + LightingPass 两个 Pass）。
- RenderGraph 语义槽位新增 `GBuffer`；LightingPass `reads(GBuffer)` + `writes(SceneColor)`，Skybox/Transparent `read_writes(SceneColor)`——**RAW/WAW/WAR 边自动保证 GBuffer → Lighting → Skybox → Transparent 的顺序**，无需人工排序。

### 4.2 G-Buffer 布局（3 个颜色附件 + 深度）

| 附件 | 格式 | 分量含义 | 设计理由 |
|------|------|----------|----------|
| RT0 | RGBA8 | rgb = Albedo（漫反射颜色，已乘纹理）<br>a = ambient_strength | albedo 天然 LDR；ambient_strength ∈ [0,1] 用 8bit 足够 |
| RT1 | RGBA16F | xyz = WorldPos（世界坐标）<br>a = spec_strength | 世界坐标 HDR 且需要精度，直接存（不做深度重建）；spec_strength ∈ [0,1] 顺手存进 alpha |
| RT2 | RGBA16F | xyz = Normal（世界空间法线）<br>a = shininess | 法线 [-1,1] 需要符号精度；shininess 范围大（1~256），16F 安全 |
| Depth | DEPTH24 | 深度 | 与现有 FrameBuffer / shadow map 深度格式一致，可 blit |

> 通道分配原则：**G-Buffer 中存的恰好是现有 `geometry.fsl` 光照公式需要的全部 per-pixel 输入**（albedo、ambient_strength、spec_strength、shininess、normal、world_pos），保证延迟光照公式与前向**逐项一致**，两路径画面互为验收基准。

### 4.3 IDRenderer 层改造（通用库能力，无引擎概念）

#### 4.3.1 FrameBufferCreateInfo：多格式数组

```cpp
struct IDR_API FrameBufferCreateInfo
{
    FrameBufferCreateInfo() = delete;
    // 单附件便捷构造（现有调用点最小改动）
    FrameBufferCreateInfo(uint32_t w, uint32_t h, TextureFormat fmt = TextureFormat::RGBA8);
    // MRT 构造：多颜色附件（上限 8，构造时校验）
    FrameBufferCreateInfo(uint32_t w, uint32_t h, std::vector<TextureFormat> fmts);

    uint32_t                     width  = 0;
    uint32_t                     height = 0;
    std::vector<TextureFormat>   color_formats;          // ★ 替代原 color_format（空 = 纯深度 FBO）
    bool                         has_depth_attachment = true;
    uint32_t                     samples = 1;
};
```

- 原 `TextureFormat color_format` 字段**删除**（编译期暴露所有使用点，逐一改为 `color_formats`，全项目已知使用点：`Renderer.cpp` 2 处、`PostProcessPass.cpp` 2 处，实施时以 `rg color_format` 全局搜索为准）。
- `TextureFormat::Depth` 不允许出现在 `color_formats` 中（构造时 `IDR_ERROR` 并剔除）。

#### 4.3.2 FrameBuffer（src 内部类）：多附件实现

```cpp
class FrameBuffer
{
    // ...
    GLuint get_color_attachment(uint32_t index = 0) const;   // 越界返回 0（GL 空纹理，采样得黑，便于发现错误）
    uint32_t get_color_attachment_count() const;             // 颜色附件数
private:
    std::vector<GLuint> m_color_texs;                        // ★ 替代 m_color_tex
};
```

实现要点（OpenGL 分支）：

- 循环创建 `color_formats.size()` 个颜色附件（`samples > 1` 时每个附件走 `glTexImage2DMultisample`，与现单附件逻辑一致）；
- **FBO 构造末尾调用 `glDrawBuffers(N, {GL_COLOR_ATTACHMENT0, ...})`**（单附件也调用，等价默认状态；这是 MRT 必需步骤——FBO 的 draw buffer 初始状态只有 `GL_COLOR_ATTACHMENT0`，不调用则 RT1/RT2 无输出）；
- `color_formats` 为空时调用 `glDrawBuffer(GL_NONE)`（纯深度 FBO 语义预留，本期 ShadowPass 仍走单附件构造，行为不变）；
- `destroy()` 循环删除全部附件纹理；移动构造/赋值改为交换 `std::vector`（空壳模式与移动语义铁律不变，见 id-renderer-design 原则 5/6）。

#### 4.3.3 RenderCommand 增强

```cpp
// 1) 深度附件 blit（延迟路径：把 G-Buffer 深度复制给场景 FBO，供 Skybox/透明物体深度测试）
void IDR_API blit_framebuffer_depth(const FrameBufferID src, const FrameBufferID dst,
    uint32_t width, uint32_t height);

// 2) 带附件索引的原生纹理句柄获取（ImGui 预览 G-Buffer 各附件用）
uint32_t IDR_API get_framebuffer_color_texture(const FrameBufferID framebuffer,
    uint32_t attachment);
//（旧单参版本保留，内部委托带索引版本，现有 ImGui Viewport 调用点不动）
```

#### 4.3.4 Pipeline 补一个只读访问器

```cpp
const VertexBufferLayout& get_layout() const;   // 构造时已存有 layout，补 getter 供 GBufferPass 建"同 layout 换 shader"的管线
```

### 4.4 RenderGraph 层改造（ID 引擎）

#### 4.4.1 RGResource 新增槽位

```cpp
enum class RGResource : uint8_t
{
    ShadowMap,
    GBuffer,        // ★ 新增：ctx.gbuffer_fb（由 GBufferPass 写入，LightingPass 读取）
    SceneColor,
    ViewportTarget,
    Count
};
```

- `RenderGraph.cpp::reset_resources()` 预填 `"GBuffer"` 槽位名。
- 死 Pass 剔除根仍是 `ViewportTarget` 写入者，GBuffer 经 `GBuffer → Lighting → SceneColor → PostProcess → ViewportTarget` 链路自动可达，**compile 算法零改动**。

#### 4.4.2 RenderContext 新增字段

```cpp
FrameBufferID gbuffer_fb = FrameBufferID::invalid_id();   // G-Buffer 渲染目标（延迟路径注入）
```

#### 4.4.3 SkyboxPass / TransparentPass 解耦 ForwardPass 硬依赖

现状两处 `builder.requires_pass<ForwardPass>()` 必须移除，理由：

- 顺序保证已由资源边覆盖：Forward（或 Lighting）`writes(SceneColor)`，Skybox/Transparent `read_writes(SceneColor)` → RAW + WAW + WAR 三重边自动排序；
- 延迟装配下若保留该硬依赖，图会**错误地自动补加一个 ForwardPass**，把 G-Buffer 之前/之外的场景颜色直接清屏重画，画面崩坏；
- 「需要场景已有初始内容」的语义由 `read_writes(SceneColor)` + compile 悬空警告（无写入者时 WARN）共同表达。

### 4.5 新增 Pass 设计

#### 4.5.1 GBufferPass（几何阶段）

```
setup:   builder.writes(RGResource::GBuffer);
execute:
  ├─ 绑定 ctx.gbuffer_fb + viewport + clear(color + depth)   // glClear 对全部 draw buffers 生效
  └─ 遍历 ctx.opaque_batches:
       ├─ resolve_pipeline(entry.pipeline)      // 管线缓存：源管线 layout/state + gbuffer shader
       ├─ material.apply()                      // 复用现有材质 uniform（u_color / texture_sampler / u_shininess ...）
       ├─ set u_mvp / u_model
       └─ draw_indexed
```

**管线缓存设计**（核心难点：现有批次的 pipeline 内嵌前向光照 shader）：

- `std::map<PipelineID, PipelineID> m_gbuffer_pipeline_cache`：源管线 → G-Buffer 管线；
- 懒创建：`PipelineManager::create({gbuffer_shader, 源管线.get_layout(), 源管线.get_pipeline_state()})`，并**强制覆写** `blend = false`（G-Buffer 阶段禁止混合）、`depth_write = true`；
- shader / pipeline 由 Pass 懒加载（参考 SkyboxPass `ensure_resources` 模式）。

#### 4.5.2 LightingPass（光照阶段）

```
setup:   builder.requires_pass<GBufferPass>();      // 硬依赖：没有几何数据光照无从谈起
         builder.reads(RGResource::GBuffer);
         builder.reads(RGResource::ShadowMap);       // 可选增强，悬空警告覆盖
         builder.writes(RGResource::SceneColor);
execute:
  ├─ ensure_resources（全屏三角形管线：depth_test=false / depth_write=false / blend=false / cull=None）
  ├─ 绑定 ctx.scene_fb + viewport
  ├─ clear(color=true, depth=true)                   // 清残留颜色
  ├─ blit_framebuffer_depth(ctx.gbuffer_fb → ctx.scene_fb)   // ★ 深度搬运，供后续 Skybox(LessEqual)/Transparent
  ├─ 绑定 G-Buffer 三个附件 → slot 0/1/2；阴影贴图 → slot 3
  ├─ set uniforms：u_camera_pos / u_ambient / u_light_count / 光源数组 ×32 / 阴影组
  └─ draw 全屏三角形
```

光源 uniform 与现有 `RenderPass::set_frame_uniforms` 同名同结构（`u_light_dirs[i]` / `u_light_positions[i]` / `u_light_colors[i]`），**仅数组上限从 8 提到 32**；逐元素 `set_param` 的名字数组模式照搬 `RenderPass.cpp` 匿名命名空间写法（上限 32）。

### 4.6 Shader 设计（4 个新文件）

| 文件 | 阶段 | 要点 |
|------|------|------|
| `assets/shader/gbuffer.vsl` | 顶点 | 复制 `geometry.vsl` 骨架：aPos/aUV/aNormal → v_uv / v_world_pos / v_normal + u_mvp / u_model（**去掉光照，顶点输出保留**） |
| `assets/shader/gbuffer.fsl` | 片段 | MRT 输出：`layout(location=0/1/2) out vec4`；uniform 与现有材质同名（`texture_sampler` / `u_color` / `u_ambient_strength` / `u_spec_strength` / `u_shininess`），保证 `MaterialInstance::apply()` 无改动直接复用 |
| `assets/shader/deferred_lighting.vsl` | 顶点 | 复制 `postprocess.vsl`（全屏三角形，v_uv 推导） |
| `assets/shader/deferred_lighting.fsl` | 片段 | 从 `geometry.fsl` **整段移植**光照函数（`calc_ambient` / `calc_diffuse` / `calc_spec_*`）与阴影函数（`find_blocker_depth` / PCF / PCSS），采样 3 张 G-Buffer 纹理重组输入后走同一套公式；`#version 440 core`，MAX_LIGHTS = 32 |

> shader 加载沿用 `ShaderManager::create(vs_path, fs_path)`，路径形式与 SkyboxPass 一致（`"../Assets/shader/xxx.vsl"`）。

### 4.7 Renderer 装配与 DevGUI

#### 4.7.1 RenderPath 状态与装配分支

```cpp
// Renderer.hpp
enum class RenderPath : uint8_t { Forward = 0, Deferred = 1 };

// Renderer
void     set_render_path(RenderPath path);     // 记录状态并按当前 visual pipeline 开关重装配
RenderPath get_render_path();
```

`set_visual_pipeline(shadow, skybox, post_process)` 内部按路径分支装配：

```
Deferred 分支：
  shadow    → add_pass<ShadowPass>()
             add_pass<GBufferPass>()            // 输出 ctx.gbuffer_fb
             add_pass<LightingPass>()           // 输出 ctx.scene_fb
  skybox    → add_pass<SkyboxPass>() + add_pass<TransparentPass>()
  post      → add_pass<PostProcessPass>()
```

- `Renderer::render()` 中 `ensure_gbuffer_fb(w, h)`（3 附件 `{RGBA8, RGBA16F, RGBA16F}` + 深度，resize 自动重建，模式与 `ensure_scene_fb` 一致），并注入 `ctx.gbuffer_fb`；
- 无后处理时的 `scene_fb → viewport_fb` 兜底拷贝、`viewport_fb → 默认 FBO` 显示拷贝：**现有逻辑对两条路径通用，零改动**。

#### 4.7.2 RendererSettingsPanel

- 新增「Render Path」下拉框（Forward / Deferred）→ `set_render_path()` + 重装配（复用现有 checkbox 状态源模式）；
- 新增「G-Buffer Debug」折叠区：3 个 `ImGui::Image`，纹理句柄来自 `get_framebuffer_color_texture(gbuffer_fb, 0/1/2)`（延迟路径激活时显示）。

### 4.8 文件布局总览（遵循 id-coding-style / id-renderer-design 目录规范）

| 操作 | 文件 | 所属步骤 |
|------|------|----------|
| 修改 | `IDLib/IDRenderer/include/Resource/FrameBuffer/FrameBufferCreateInfo.hpp` | Step 1 |
| 修改 | `IDLib/IDRenderer/src/Resource/FrameBuffer/FrameBuffer.hpp` / `.cpp` | Step 1 |
| 修改（调用点） | `ID/src/Renderer/Render/Renderer.cpp`、`ID/src/Renderer/Render/RenderPass/PostProcessPass.cpp` | Step 1 |
| 修改 | `IDLib/IDRenderer/include/Render/RenderCommand.hpp` | Step 2 |
| 修改 | `IDLib/IDRenderer/src/Render/IDRCmdOpenGLImpl.cpp` | Step 2 |
| 修改 | `IDLib/IDRenderer/src/Resource/Pipeline/Pipeline.hpp` | Step 2 |
| 修改 | `ID/include/Renderer/Render/RenderGraph/RGTypes.hpp`、`ID/src/Renderer/Render/RenderGraph/RenderGraph.cpp` | Step 3 |
| 修改 | `ID/include/Renderer/Render/RenderContext.hpp` | Step 3 |
| 修改 | `ID/src/Renderer/Render/RenderPass/SkyboxPass.cpp`、`TransparentPass.cpp` | Step 4 |
| 新增 | `assets/shader/gbuffer.vsl` / `gbuffer.fsl` | Step 5 |
| 新增 | `ID/include/Renderer/Render/RenderPass/GBufferPass.hpp`、`ID/src/Renderer/Render/RenderPass/GBufferPass.cpp` | Step 5 |
| 新增 | `assets/shader/deferred_lighting.vsl` / `deferred_lighting.fsl` | Step 6 |
| 新增 | `ID/include/Renderer/Render/RenderPass/LightingPass.hpp`、`ID/src/Renderer/Render/RenderPass/LightingPass.cpp` | Step 6 |
| 修改 | `ID/include/Renderer/Render/Renderer.hpp`、`ID/src/Renderer/Render/Renderer.cpp` | Step 3 / 7 |
| 修改 | `ID/src/DevGUI/ImGui/Panels/RendererSettingsPanel.cpp` | Step 7 |
| 更新 | `.pi/agent/ID_ENGINE_ARCHITECTURE_PLAN.md`、本计划书状态跟踪 | Step 8 |

> ID 库 CMake 使用 `file(GLOB_RECURSE)` 收集源文件，新增 `.cpp` **无需改 CMake**。

---

## 5. 分步实施计划

> 执行顺序严格按 Step 编号推进。每步验收通过（含用户确认）后才可进入下一步。

---

### Step 1：IDRenderer — FrameBuffer MRT 支持

#### 目标

`FrameBufferCreateInfo` 支持多颜色附件格式数组；`FrameBuffer`（OpenGL 分支）真正创建 N 个颜色附件并在 FBO 构造时调用 `glDrawBuffers`。**现有单附件调用点行为完全不变**。

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 修改 | `IDLib/IDRenderer/include/Resource/FrameBuffer/FrameBufferCreateInfo.hpp` |
| 修改 | `IDLib/IDRenderer/src/Resource/FrameBuffer/FrameBuffer.hpp` |
| 修改 | `IDLib/IDRenderer/src/Resource/FrameBuffer/FrameBuffer.cpp` |
| 修改（调用点） | `ID/src/Renderer/Render/Renderer.cpp`（`ensure_scene_fb` / `ensure_viewport_fb`） |
| 修改（调用点） | `ID/src/Renderer/Render/RenderPass/PostProcessPass.cpp`（bloom 中间缓冲 ×2） |

#### 任务明细

1. `FrameBufferCreateInfo` 按 4.3.1 重构：`color_format` → `std::vector<TextureFormat> color_formats`；提供单格式便捷构造（默认 `RGBA8`，现有 `FrameBufferCreateInfo(w, h)` 调用点零改动）与 vector 构造；构造内校验：附件数 ≤ 8（超限 `IDR_ERROR` 截断）、剔除 `TextureFormat::Depth`（`IDR_ERROR`）。
2. `FrameBuffer`：`m_color_tex` → `std::vector<GLuint> m_color_texs`；`get_color_attachment(index)` 读 vector（越界返回 0）；新增 `get_color_attachment_count()`。
3. `FrameBuffer.cpp` 构造函数：颜色附件创建逻辑改循环（`samples > 1` 每附件 `glTexImage2DMultisample`，否则 `glTexImage2D`，过滤/包裹参数与现实现一致）；FBO 完整性检查前调用 `glDrawBuffers`（≥1 附件传 `GL_COLOR_ATTACHMENT0 + i` 数组；0 附件 `glDrawBuffer(GL_NONE)`）。
4. `destroy()` / 移动构造 / 移动赋值适配 vector（`std::vector` 交换/清空即可，空壳与所有权转移语义保持 id-renderer-design 原则 5/6）。
5. 全局搜索 `color_format`（`rg color_format ID IDLib`），逐点改为 `color_formats`（已知 4 处，见涉及文件）。

#### 验收清单

- [ ] `rg "color_format\b" ID IDLib` 无旧字段残留（`color_formats` 不算）
- [ ] 公开头文件 `FrameBufferCreateInfo.hpp` 无平台依赖（glad 只出现在 `src/`）
- [ ] 新成员 `m_color_texs` 有默认值（`= {}` 或声明即空）；>3 行函数体全部在 `.cpp`
- [ ] 单附件 FBO（scene/viewport/bloom/shadow）创建日志与之前一致，无新增 `IDR_ERROR`
- [ ] **【请用户参与】** 用户编译（PowerShell）并运行 Sandbox：**前向画面与改造前完全一致**（重点观察：视口画面、bloom、阴影、窗口 resize 时的 FBO 重建日志）
- [ ] 用户确认通过，方可进入 Step 2

#### ⚠️ 进入 Step 2 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`（重点：编译纪律）；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`（重点：RenderCommand 职责、平台隔离）；
> ⑤ **本计划书全文**（重点：第 2 节执行纪律、第 4.3.3 节、Step 2 章节）。

---

### Step 2：IDRenderer — RenderCommand 深度 blit / 索引纹理获取 / Pipeline get_layout

#### 目标

补齐延迟路径需要的三个通用渲染命令：深度 blit、带附件索引的纹理句柄获取、Pipeline layout 访问器。**本步为纯增量，无任何现有行为变化。**

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 修改 | `IDLib/IDRenderer/include/Render/RenderCommand.hpp` |
| 修改 | `IDLib/IDRenderer/src/Render/IDRCmdOpenGLImpl.cpp` |
| 修改 | `IDLib/IDRenderer/src/Resource/Pipeline/Pipeline.hpp` |

#### 任务明细

1. `RenderCommand.hpp` 新增声明（位置放在 `blit_framebuffer_to_default` 之后，与现有 blit 系列相邻）：
   - `void blit_framebuffer_depth(const FrameBufferID src, const FrameBufferID dst, uint32_t width, uint32_t height);`
   - `uint32_t get_framebuffer_color_texture(const FrameBufferID framebuffer, uint32_t attachment);`
2. `IDRCmdOpenGLImpl.cpp` 实现（`#ifdef IDRENDERER_USE_OPENGL` 内，紧随现有 blit 实现）：
   - 深度 blit：`glBlitFramebuffer(..., GL_DEPTH_BUFFER_BIT, GL_NEAREST)`，无效 ID / 零尺寸 early-return，模式照抄 `blit_framebuffer`；
   - 索引版句柄获取：内部 `get_color_attachment(attachment)`，`framebuffer` 无效时返回 0；**旧单参版本保留并委托新版本**；
   - 两个 blit 结束后的 `glBindFramebuffer(GL_FRAMEBUFFER, 0)` 解绑行为与现有实现保持一致。
3. `Pipeline.hpp` 新增 `const VertexBufferLayout& get_layout() const;`（一行 getter，读构造时保存的 layout 成员；成员名以现有代码为准）。

#### 验收清单

- [ ] `RenderCommand.hpp` 中新旧 `get_framebuffer_color_texture` 重载共存且编译通过（重载不歧义：单参 vs 双参）
- [ ] 平台头文件（glad）未泄漏到 `include/`
- [ ] **【请用户参与】** 用户编译并运行：前向画面与 Step 1 验收时一致（本步纯增量，理论上零变化；ImGui Viewport 面板图像正常说明旧句柄获取路径未破坏）
- [ ] 用户确认通过，方可进入 Step 3

#### ⚠️ 进入 Step 3 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`（重点：RGResource 槽位语义在 ID 库侧）；
> ⑤ **本计划书全文**（重点：第 4.4 节、Step 3 章节）。

---

### Step 3：ID 引擎 — RG 槽位 / RenderContext 字段 / G-Buffer FBO 与 RenderPath 状态

#### 目标

RenderGraph 侧挂上 `GBuffer` 槽位；RenderContext 挂上 `gbuffer_fb` 字段；Renderer 增加 `ensure_gbuffer_fb` 与 `RenderPath` 枚举状态（**本步只存状态，Deferred 装配分支留到 Step 7**）。前向画面零变化。

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 修改 | `ID/include/Renderer/Render/RenderGraph/RGTypes.hpp` |
| 修改 | `ID/src/Renderer/Render/RenderGraph/RenderGraph.cpp`（`reset_resources`） |
| 修改 | `ID/include/Renderer/Render/RenderContext.hpp` |
| 修改 | `ID/include/Renderer/Render/Renderer.hpp` |
| 修改 | `ID/src/Renderer/Render/Renderer.cpp` |

#### 任务明细

1. `RGTypes.hpp`：`RGResource` 枚举在 `ShadowMap` 与 `SceneColor` 之间插入 `GBuffer`（注释与现有风格一致：`ctx.gbuffer_fb（由 GBufferPass 写入）`）。
2. `RenderGraph.cpp::reset_resources`：追加 `resources[GBuffer].name = "GBuffer";`。
3. `RenderContext.hpp`：在 `scene_fb` 之前新增 `FrameBufferID gbuffer_fb = FrameBufferID::invalid_id();`（带注释）。**注意 RenderContext 是聚合初始化**——`Renderer::render()` 中 `RenderContext ctx{...}` 的初始化列表需同步插入 `gbuffer_fb`（编译器会指出位置）。
4. `Renderer.hpp/.cpp`：
   - 定义 `enum class RenderPath : uint8_t { Forward = 0, Deferred = 1 };`（Renderer.hpp，`namespace ID::Renderer` 内）；
   - `RenderPath& render_path_state()`（匿名命名空间 static 模式，与 `post_process_enabled_flag` 一致）+ `set_render_path()` / `get_render_path()` 公开 API（本步 set 时仅记状态 + `ID_INFO` 日志，**不触发重装配**——Step 7 再接）；
   - 匿名命名空间新增 `GBufferFBState` + `ensure_gbuffer_fb(w, h)`（模式照抄 `ensure_scene_fb`：尺寸不匹配时销毁重建；`FrameBufferCreateInfo(w, h, std::vector<TextureFormat>{RGBA8, RGBA16F, RGBA16F})`，保留深度附件，`samples = 1`）；
   - `render()` 中与 `ensure_scene_fb` 并列调用 `ensure_gbuffer_fb`，并注入 `ctx.gbuffer_fb`。

#### 验收清单

- [ ] `RGGraphView::resource_names` 自动含 `"GBuffer"`（DevGUI 节点编辑器资源列表出现新槽位）
- [ ] `RenderContext` 聚合初始化点已同步（编译通过即验证）
- [ ] `ensure_gbuffer_fb` 的重建日志模式与 `ensure_scene_fb` 一致（resize 窗口时各出现一次）
- [ ] 编译日志无新增 RG 警告（GBuffer 槽位无人读写，不触发悬空警告——悬空只针对 reads）
- [ ] **【请用户参与】** 用户编译并运行：前向画面一致；拖拽窗口 resize，日志出现「G-Buffer FBO 重建 WxH」且画面正常
- [ ] 用户确认通过，方可进入 Step 4

#### ⚠️ 进入 Step 4 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`；
> ⑤ **本计划书全文**（重点：第 4.4.3 节解耦理由——这是本步的灵魂，务必理解后再动手）。

---

### Step 4：现有 Pass 解耦 — 移除 SkyboxPass / TransparentPass 对 ForwardPass 的硬依赖

#### 目标

为延迟装配扫清障碍：Skybox / Transparent 不再 `requires_pass<ForwardPass>()`，顺序完全交给 SceneColor 资源边自动推导。**前向各开关组合的执行序与画面必须完全不变。**

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 修改 | `ID/src/Renderer/Render/RenderPass/SkyboxPass.cpp`（`setup`） |
| 修改 | `ID/src/Renderer/Render/RenderPass/TransparentPass.cpp`（`setup`） |

#### 任务明细

1. `SkyboxPass::setup`：删除 `builder.requires_pass<ForwardPass>();` 一行，注释改为说明「顺序由 SceneColor 的 RAW/WAW/WAR 边保证：Forward 或 Lighting 写入后本 Pass 才读改写；深度取自当前绑定的场景 FBO（前向 = ForwardPass 写入，延迟 = LightingPass 从 G-Buffer blit）」。
2. `TransparentPass::setup`：同样删除该行，`read_writes(SceneColor)` + `reads(ShadowMap)` 保留，注释说明同上（透明混合需要场景已有不透明内容，该语义由资源边表达）。
3. 不改两个 Pass 的 `execute`（它们绑定的 `ctx.scene_fb` 与深度来源在两条路径下都正确——延迟路径的深度由 Step 6 的 LightingPass blit 注入）。

#### 验收清单

- [ ] `rg "requires_pass<ForwardPass>" ID/src` 结果为空
- [ ] **【请用户参与】** 用户编译并运行，逐项核对：
  - 前向默认装配（shadow/skybox/post 全开）画面与 Step 3 时一致；
  - Console 面板的 RG 编译日志执行序仍为 `Shadow → Forward → Skybox → Transparent → PostProcess`；
  - 关闭 skybox 后：`Shadow → Forward(含透明) → PostProcess`，透明物体渲染正常。
- [ ] 用户确认通过，方可进入 Step 5

#### ⚠️ 进入 Step 5 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`（重点：新增资源检查清单——虽然 GBufferPass 在 ID 库，但引用 IDRenderer API 的方式参照现有 Pass）；
> ⑤ **本计划书全文**（重点：第 4.2 节 G-Buffer 布局、第 4.5.1 节、第 4.6 节、Step 5 章节）。

---

### Step 5：G-Buffer shader + GBufferPass

#### 目标

写出延迟路径的几何阶段：`gbuffer.vsl/.fsl`（MRT 输出，材质 uniform 与现有同名）与 `GBufferPass`（含管线缓存）。**本步 Pass 尚未装配进任何路径，运行时行为零变化。**

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 新增 | `assets/shader/gbuffer.vsl` |
| 新增 | `assets/shader/gbuffer.fsl` |
| 新增 | `ID/include/Renderer/Render/RenderPass/GBufferPass.hpp` |
| 新增 | `ID/src/Renderer/Render/RenderPass/GBufferPass.cpp` |

#### 任务明细

1. `gbuffer.vsl`：以 `geometry.vsl` 为模板，保留 `aPos(0)/aUV(1)/aNormal(2)` 输入、`u_mvp/u_model`、`v_uv/v_world_pos/v_normal` 输出（逐行对照，只删光照相关）。
2. `gbuffer.fsl`：
   - 顶部注释块按 `postprocess.fsl` 风格写明 3 个附件的布局（对齐 4.2 节表格）；
   - `layout(location = 0) out vec4 GBuffer0;`（rgb = albedo × 纹理，a = ambient_strength）等三个输出；
   - uniform **必须**与 `geometry.fsl` 材质段同名：`texture_sampler` / `u_color` / `u_ambient_strength` / `u_spec_strength` / `u_shininess`（纹理与颜色的混合方式参照 `geometry.fsl` main 的写法，保持一致）；
   - 无纹理材质路径：与 `geometry.fsl` 一致（材质未绑纹理时输出纯 `u_color`——现有机制由 `draw_batch` 的 `unbind_texture(0)` 保证，GBufferPass 循环内同样先 `unbind_texture(0)`）。
3. `GBufferPass.hpp/.cpp`（类结构参照 `ForwardPass` / `SkyboxPass`）：
   - `GBufferPass()` 默认构造；`setup`：`builder.writes(RGResource::GBuffer);`
   - 私有：`ShaderID m_shader`、`std::map<PipelineID, PipelineID> m_pipeline_cache`、`void ensure_resources();`、`PipelineID resolve_pipeline(const PipelineID src);`
   - `resolve_pipeline`：缓存命中返回；否则用 Step 2 的 `get_layout()` + `get_pipeline_state()`（**强制覆写 `blend = false`、`depth_write = true`**）+ `gbuffer` shader 创建新管线入缓存（`PipelineManager::create`）；
   - `execute`（对齐 4.5.1 流程）：`ensure_resources()` → 绑 `ctx.gbuffer_fb`（无效则 `ID_WARN` + return）→ viewport → `IDRCmd::clear()` → 遍历 `ctx.opaque_batches`：`unbind_texture(0)` → `material.apply()` → `set_param(u_mvp / u_model)`（参照 `RenderPass::set_object_uniforms`，直接内联两行或复用该静态方法——复用优先）→ `draw_indexed` → 统计宏；
   - shader 懒加载：`ShaderManager::create("../Assets/shader/gbuffer.vsl", "../Assets/shader/gbuffer.fsl")`，失败 `ID_ERROR` 并置无效。
4. `ID.hpp` 伞形头文件确认：RenderPass 目录下的头文件如为逐个 include 则补 `GBufferPass.hpp`（若为目录聚合则确认聚合头覆盖）。

#### 验收清单

- [ ] `gbuffer.fsl` 的 uniform 名与 `geometry.fsl` 材质段逐一同名（人工对照）
- [ ] `GBufferPass` 公开接口带 `ID_API`；>3 行函数体在 `.cpp`；成员全部有默认值
- [ ] Pass 未被装配（`rg "add_pass<GBufferPass>" ID` 为空——Step 7 才接入），运行时零行为变化
- [ ] 头文件无 glad 等平台依赖
- [ ] **【请用户参与】** 用户编译并运行：前向画面与 Step 4 验收时一致（再次确认零行为变化）
- [ ] 用户确认通过，方可进入 Step 6

#### ⚠️ 进入 Step 6 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`；
> ⑤ **本计划书全文**（重点：第 4.5.2 节、第 4.6 节、Step 6 章节；特别是「光照函数从 geometry.fsl 整段移植，不得凭记忆重写」）。

---

### Step 6：延迟光照 shader + LightingPass

#### 目标

写出延迟路径的光照阶段：`deferred_lighting.vsl/.fsl` 与 `LightingPass`（全屏三角形采样 G-Buffer + 阴影，输出 HDR SceneColor，并完成 G-Buffer 深度 → 场景 FBO 的搬运）。**本步 Pass 仍未装配，运行时行为零变化。**

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 新增 | `assets/shader/deferred_lighting.vsl` |
| 新增 | `assets/shader/deferred_lighting.fsl` |
| 新增 | `ID/include/Renderer/Render/RenderPass/LightingPass.hpp` |
| 新增 | `ID/src/Renderer/Render/RenderPass/LightingPass.cpp` |

#### 任务明细

1. `deferred_lighting.vsl`：直接复制 `postprocess.vsl`（全屏三角形 + v_uv 推导），仅改文件头注释。
2. `deferred_lighting.fsl`（**光照公式移植铁律：打开 `assets/shader/geometry.fsl` 与之并排逐段移植，禁止凭记忆重写**）：
   - 移植 `calc_ambient / calc_diffuse / calc_spec_phong / calc_spec_blinn_phong` 及 main 中对它们的调用组合方式（保持与前向一致的选择）；
   - 移植阴影段：`find_blocker_depth` / blocker 估计 / PCF / PCSS 全套 + `u_shadow_map / u_light_space_mvp / u_shadow_enabled / u_shadow_bias / u_shadow_pcf_radius / u_shadow_light_index / u_light_size` 同名 uniform；
   - 输入改采样：`u_gbuffer_albedo(0)` / `u_gbuffer_pos(1)` / `u_gbuffer_normal(2)` / `u_shadow_map(3)`，从三张纹理重组 `albedo / ambient_strength / world_pos / spec_strength / normal / shininess`；
   - 光源数组上限 **32**：`u_light_dirs[32]` / `u_light_positions[32]` / `u_light_colors[32]` / `u_light_count`；`u_camera_pos` / `u_ambient`；
   - 输出 `out vec4 FragColor;`（HDR，线性空间，与 ForwardPass 输出一致——不做 tonemap/gamma，那是 PostProcessPass 的职责）。
3. `LightingPass.hpp/.cpp`：
   - 构造 `LightingPass(const Vec3& ambient = Vec3(1,1,1))`；
   - `setup`：`requires_pass<GBufferPass>()` + `reads(GBuffer)` + `reads(ShadowMap)` + `writes(SceneColor)`（对齐 4.5.2）；
   - 私有：`ShaderID` / `PipelineID` / `ensure_resources()`（全屏三角形管线：`FullscreenQuad::vertex_buffer()` + `FullscreenQuad::layout()`，state：`depth_test = false`、`depth_write = false`、`blend = false`、`cull_mode = None`，参照 `PostProcessPass::ensure_resources`）；
   - `execute`（对齐 4.5.2 流程）：绑 `ctx.scene_fb` → viewport → `clear(true, true)` → `blit_framebuffer_depth(ctx.gbuffer_fb, ctx.scene_fb, w, h)`（Step 2 新命令；src/dst 任一无效则跳过 + `ID_WARN`）→ `bind_framebuffer_color(ctx.gbuffer_fb, 0/1/2 → slot 0/1/2)` → 阴影启用时 `bind_framebuffer_depth(ctx.shadow_fb, 3)` 并设置阴影 uniform（照抄 `RenderPass::apply_shadow` 的 uniform 名与值，注意 `u_shadow_map` 设为 3）→ 设 `u_camera_pos / u_ambient / u_light_count / 光源数组`（逐元素 set_param，名字数组照 `RenderPass.cpp` 匿名命名空间模式，上限 32；超过 32 时 `ID_WARN` 截断）→ `draw_arrays(m_pipeline, FullscreenQuad::vertex_buffer())` → 统计宏（draw_calls +1）。
4. `ID.hpp` / 聚合头补 `LightingPass.hpp`（与 Step 5 同一位置检查）。

#### 验收清单

- [ ] 光照/阴影函数与 `geometry.fsl` 并排对照一致（函数体 diff 级一致，仅输入来源不同）
- [ ] `MAX_LIGHTS = 32` 常量在 `LightingPass.hpp` 定义且与 fsl 数组长度一致（注释双向锚定）
- [ ] Pass 未装配（`rg "add_pass<LightingPass>" ID` 为空）
- [ ] **【请用户参与】** 用户编译并运行：前向画面与 Step 5 验收时一致（零行为变化确认）
- [ ] 用户确认通过，方可进入 Step 7

#### ⚠️ 进入 Step 7 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`；
> ⑤ **本计划书全文**（重点：第 4.7 节、Step 7 章节。Step 7 是首个运行时行为变化步骤，验收项最多，用户参与最深）。

---

### Step 7：装配 Deferred 路径 + RendererSettingsPanel 切换 + G-Buffer 预览 ★ 用户重点验收步

#### 目标

打通完整延迟路径：装配分支、运行时路径切换 UI、G-Buffer 调试预览。**本步是首个产生可见画面变化的步骤，所有验收以用户实际操作为准。**

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 修改 | `ID/src/Renderer/Render/Renderer.cpp`（装配分支 / set_render_path 接入重装配） |
| 修改 | `ID/include/Renderer/Render/Renderer.hpp`（如需暴露 `get_gbuffer_fb()`） |
| 修改 | `ID/src/DevGUI/ImGui/Panels/RendererSettingsPanel.cpp` |

#### 任务明细

1. `Renderer.cpp`：
   - `set_visual_pipeline(shadow, skybox, post)` 开头读取 `render_path_state()` 分支：`Forward` 分支保持现有装配代码不动；`Deferred` 分支按 4.7.1 装配（`GBufferPass` + `LightingPass` 必装，`LightingPass` 构造传入环境光参数——可暂用固定白色，或复用 Forward 默认值）；
   - `set_render_path(path)`：更新状态后调用与 `set_visual_pipeline` 相同的重装配流程（把三开关状态与装配逻辑收拢为一个内部函数 `rebuild_pipeline(shadow, skybox, post)` 是可接受的小重构，避免两处复制装配代码）；
   - 装配完成后照旧 `graph.compile()` + `ID_INFO` 日志（日志追加 `path=Deferred/Forward` 字段）。
2. `Renderer.hpp/.cpp`：新增 `FrameBufferID get_gbuffer_fb();`（照抄 `get_viewport_fb` 模式，供 DevGUI 预览）。
3. `RendererSettingsPanel.cpp`：
   - 顶部新增 `ImGui::RadioButton("Forward", ...)` / `("Deferred", ...)` 或 Combo（`int path` 状态 ↔ `Renderer::get/set_render_path`），切换后走统一重装配回调（复用现有 `changed` 模式）；
   - 新增折叠区「G-Buffer Debug（Deferred）」：仅当 `get_render_path() == Deferred` 且 `get_gbuffer_fb()` 有效时显示；三个 `ImGui::Image`（附件 0/1/2），句柄用 `RenderCommand::get_framebuffer_color_texture(fb, i)`，每图带标签（Albedo+AmbientA / WorldPos+SpecA / Normal+ShinA），尺寸用现有 Viewport 面板的缩放模式。
4. 无后处理兜底：确认 `Renderer::render()` 现有 `!post_process_enabled_flag()` 分支的 `scene_fb → viewport_fb` 拷贝对延迟路径同样生效（LightingPass 写的就是 `ctx.scene_fb`，无需改动，验收确认即可）。

#### 验收清单（★ 本步全部需要用户实际操作验收）

- [ ] **【请用户参与】** 编译并运行，按下列顺序验收并在结果上打勾反馈给 AI：
  - [ ] **A. 路径切换**：Settings 面板切到 Deferred——Console 出现 `path=Deferred` 装配日志，执行序为 `Shadow → GBuffer → Lighting → Skybox → Transparent → PostProcess`（按开关组合裁剪）；切回 Forward 执行序与画面恢复原样；
  - [ ] **B. 画面正确性**：Deferred 下不透明物体光照与 Forward **肉眼近似一致**（albedo / 高光 / 阴影 / 环境光逐项对比；允许深度精度导致的极细微差异，不允许明显色偏、法线错向、漏光）；
  - [ ] **C. 阴影**：Deferred + Shadow 开启，阴影方向与柔和度和 Forward 一致（PCSS 参数相同）；
  - [ ] **D. 天空与透明**：Deferred + Skybox：天空盒只填充背景（不遮挡几何）；透明物体正确混合在光照结果之上；
  - [ ] **E. 多光源优势**：在场景中放置 **10 个以上**光源——Deferred 全部生效，Forward 只亮前 8 个（这正是本项目的招牌验收点）；
  - [ ] **F. G-Buffer 预览**：Debug 折叠区三张图直观正确（RT0 应见物体本色、RT1 看起来像位置渐变、RT2 法线图彩色）；
  - [ ] **G. resize**：拖拽窗口尺寸，G-Buffer/场景/显示 FBO 重建日志各一次，画面无拉伸错乱；
  - [ ] **H. 回归**：Forward 路径 16 组合（shadow×skybox×post）抽查至少 5 组，画面与改造前一致。
- [ ] 验收全部通过后，方可进入 Step 8；**任何一项不通过，AI 应先诊断（读代码 + 日志），列出怀疑点与修复方案，经用户同意后修复**

#### ⚠️ 进入 Step 8 前必读提醒

> **执行下一步骤的 AI 必须重新阅读以下内容（防止上下文遗忘）：**
> ① `.pi/agent/AGENTS.md`；
> ② 全局 skill `yy-coding`（含 `cpp-clangformat.md`）；
> ③ 本地 skill `.pi/skills/id-coding-style/SKILL.md`；
> ④ 本地 skill `.pi/skills/id-renderer-design/SKILL.md`；
> ⑤ **本计划书全文**（重点：第 6 节风险与开放问题、Step 8 章节）。

---

### Step 8：全组合回归 + 文档收尾

#### 目标

系统性回归两条路径 × 全部开关组合，更新架构计划书与本计划书状态，为后续优化（G-Buffer 压缩、Tiled Lighting）留好接口注记。

#### 涉及文件

| 操作 | 文件 |
|------|------|
| 更新 | `.pi/agent/ID_ENGINE_ARCHITECTURE_PLAN.md`（新增延迟渲染章节：路径图、G-Buffer 布局、Pass 清单） |
| 更新 | 本计划书第 8 节「状态跟踪」与第 7 节「变更记录」 |
| 更新 | `ID/src/DevGUI/ImGui/Panels/RendererSettingsPanel.cpp` 中 Pass 说明表（追加 GBufferPass / LightingPass 行）——若该表存在 |

#### 任务明细

1. 用户按下方矩阵执行回归（AI 提供清单，用户反馈结果）：
   - 路径 {Forward, Deferred} × 阴影 {0,1} × 天空 {0,1} × 后处理 {0,1} = 16 组合；
   - 每组合观察：画面完整、Console 无 `ID_ERROR`、RG 日志执行序合理；
   - 重点补充：Deferred + 无后处理（验证 scene→viewport 兜底拷贝）、Deferred + 无阴影（验证 `u_shadow_enabled = 0` 跳过阴影路径）、空场景（无提交批次时 LightingPass 全屏三角形只输出环境色，G-Buffer 为清屏值不崩溃）。
2. RG 调试资产检查：`export_graphviz` 导出 Deferred 装配的 `.dot`（若 DevGUI 有该按钮），确认 GBuffer 节点与 RAW 边（GBuffer → Lighting）出现在图中；节点编辑器面板可见 GBuffer 槽位。
3. 更新 `.pi/agent/ID_ENGINE_ARCHITECTURE_PLAN.md`：按 AGENTS.md 要求「落地实现后及时更新计划书」。
4. 填写本计划书状态跟踪表（每步状态 + 完成日期），变更记录补记实施期间的所有设计偏离。

#### 验收清单

- [ ] 16 组合回归全部通过（用户反馈确认）
- [ ] GraphViz / 节点编辑器正确展示 GBuffer 槽位与依赖边
- [ ] `ID_ENGINE_ARCHITECTURE_PLAN.md` 已更新延迟渲染章节
- [ ] 本计划书状态跟踪已填写完毕
- [ ] **项目完成 🎉（由用户宣布）**

---

## 6. 风险与开放问题

| # | 风险 / 问题 | 影响步骤 | 缓解措施 |
|---|-------------|----------|----------|
| 1 | `gbuffer.fsl` 材质 uniform 名与现有材质库不一致 → `MaterialInstance::apply()` 写入无效 | Step 5 | 实施时并排对照 `geometry.fsl`；验收清单已含人工对照项 |
| 2 | `m_pipeline_cache` 的 key（源 PipelineID）失效后缓存悬空（源管线被销毁复用同 ID） | Step 5 / 7 | 现阶段材质库管线生命周期与进程同长，风险低；在 GBufferPass 头文件注释中记录该假设；若未来支持管线销毁，需监听失效或改弱引用 |
| 3 | 深度 blit 要求 src/dst 深度内部格式一致（均为 DEPTH24）→ 若某方换格式 blit 失败 | Step 6 | 本期两处深度格式统一 DEPTH24；`blit_framebuffer_depth` 失败时 GL 报错可在 RenderDoc / 日志观察，`ID_WARN` 兜底提示 |
| 4 | Skybox 深度依赖 LightingPass 的 blit；若装配了 Skybox 却没装 Lighting（用户手拼装配）→ 天空深度错误 | Step 7 | `set_visual_pipeline` 的 Deferred 分支保证 LightingPass 必装；悬空警告覆盖自由拼装场景 |
| 5 | 光源数组 uniform 上限 32 × 3 组 vec3 数组的 uniform 数量超限（fragment 阶段保证下限 224 vec4，3×32=96 + 阴影/杂项 < 128，安全） | Step 6 | 已核算安全；若未来提上限需换 UBO（`LightDataGLSL` std140 已有现成结构，见开放问题 2） |
| 6 | `glDrawBuffers` 忘记调用 → RT1/RT2 全黑 | Step 1 | 已列入 FrameBuffer 构造实现要点；G-Buffer 预览面板可立即暴露该错误 |
| 7 | Deferred 路径下 MSAA（`samples > 1`）G-Buffer 未验证 | 全局 | 非目标；`ensure_gbuffer_fb` 固定 `samples = 1` |
| **开放问题 1** | 光照 uniform 双轨制：`RenderPass::set_frame_uniforms`（独立 uniform 数组）vs `LightUniforms.hpp`（std140 UBO `LightDataGLSL`）并存 | — | 建议后续统一为 UBO（一次上传全阶段共享）；本期沿用独立 uniform 与前向保持一致，避免扩大改动面 |
| **开放问题 2** | G-Buffer 带宽优化（RGBA16F×2 较宽） | — | 后续可做法线八面体编码 + 深度重建世界坐标，压缩为 RGBA8×2；本期正确性优先 |
| **开放问题 3** | 透明物体在 Deferred 路径仍走前向光照（`MAX_LIGHTS = 8`） | — | 业界通行做法；后续可让透明 shader 采样 G-Buffer 的"最近不透明深度/法线"近似 |

---

## 7. 变更记录

> 实施期间任何与本计划书的设计偏离，必须先记录于此并向用户汇报，经同意后方可继续。

| 日期 | 步骤 | 变更内容 | 原因 | 用户确认 |
|------|------|----------|------|----------|
| 2026-02-27 | Step 2 | Pipeline.hpp 不新增 `get_layout()`：现有 `get_vertex_buffer_layout()` 已返回 `const VertexBufferLayout&`（构造时保存的 m_layout），完全满足 GBufferPass 需求，避免冗余 getter；Step 5 直接复用现有访问器 | 最小改动原则（karpathy：不添加多余代码） | 用户确认 |
| 2026-02-27 | Step 5 | 材质参数应用不直接调用 `MaterialInstance::apply()`：该函数内部以材质自身 shader 查询 uniform location，而 `set_param` 的 `glUniform` 作用于当前绑定的 program，材质 shader 与 gbuffer shader 是两个不同 program，location 分配不一致会导致 uniform 写入错位（no-op 或写错位置）；改为 GBufferPass 私有 `apply_material()` 以 gbuffer shader 为目标应用同一套合并逻辑（父级默认 + 局部覆盖 + 纹理绑定），并在每批次先 `bind_pipeline` 确保 gbuffer program 已绑定 | 正确性优先（Step 7 验收 B 要求画面与前向一致） | 待用户确认 |
| 2026-02-27 | Step 5 | 修复编译错误：`IDR_ResPipeline` 是 IDRenderer src 内部头（ResourceGetter.hpp）的宏，ID 引擎层不可见；任务书 4.5.1 假设 ID 引擎能访问源管线对象不成立。补救：RenderCommand 公开 API 新增 `get_pipeline_layout(pipeline)` / `get_pipeline_state(pipeline)`（与 get_framebuffer_color_texture 同风格的通用库能力），GBufferPass::resolve_pipeline 改用公开 API | 编译必需的库能力补充（RenderCommand.hpp / IDRCmdOpenGLImpl.cpp / GBufferPass.cpp） | 用户确认 |
| 2026-02-27 | Step 5.5 | 修复 RenderGraphEditorWidget ghost 节点按钮 ID 冲突（Phase 4a 遗留）：ghost 的 "Enable" 按钮缺 PushID，同时禁用多个 Pass（多 ghost 并存）时 Dear ImGui 报 "conflicting ID"；补 `PushID(0x10000000u + g)` 与实节点 x 按钮风格一致。ForwardPass 无 × 按钮是既有设计（ghost 列表刻意不登记，常开不可关），未改动 | 用户实测发现的既有 bug，影响 Step 7 组合验证可用性（文件：RenderGraphEditorWidget.cpp，超出任务书涉及文件范围，经用户报告后修复） | 用户确认 |
| 2026-02-27 | Step 7 | Deferred 下物体纯黑诊断与修复：`blit_framebuffer_depth` 实现末尾 `glBindFramebuffer(GL_FRAMEBUFFER, 0)` 把 framebuffer 解绑为默认窗口，LightingPass 在 blit 后未重新绑定 scene_fb → 全屏三角形绘制到默认 FBO，scene_fb 保持清屏黑色 → PostProcess 输入黑 → 视口物体区纯黑（G-Buffer 预览正常佐证几何阶段无误）。修复：blit 后重新 `bind_framebuffer(ctx.scene_fb)`。另确认 `set_param` 模板版内部自带 `bind_pipeline`/`bind_shader`（SetParamImpl.hpp），uniform 写入目标本无问题，LightingPass 开头显式 bind_pipeline 为无害保险 | 正确性修复（文件：LightingPass.cpp） | 待用户确认 |

---

## 8. 状态跟踪

| 步骤 | 内容 | 状态 | 完成日期 | 备注 |
|------|------|------|----------|------|
| Step 1 | FrameBuffer MRT | ⬜ 未开始 | — | |
| Step 2 | RenderCommand / Pipeline 增强 | ⬜ 未开始 | — | |
| Step 3 | RG 槽位 / ctx / RenderPath 状态 | ⬜ 未开始 | — | |
| Step 4 | Skybox/Transparent 解耦 | ⬜ 未开始 | — | |
| Step 5 | gbuffer shader + GBufferPass | ⬜ 未开始 | — | |
| Step 6 | lighting shader + LightingPass | ⬜ 未开始 | — | |
| Step 7 | 装配 + UI 切换 + 预览 ★ | ⬜ 未开始 | — | 用户重点验收 |
| Step 8 | 全组合回归 + 文档 | ⬜ 未开始 | — | |

> 状态标记：⬜ 未开始 / 🔄 进行中 / ✅ 已验收通过 / ⏸ 阻塞（备注原因）
