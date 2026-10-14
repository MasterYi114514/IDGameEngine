# AGENTS.md — IDGameEngine AI 开发指南

欢迎来到 **IDGameEngine**（纯自研 C++20 高性能微内核游戏引擎）。任何在此仓库中进行架构设计、编写代码、修改 CMake 或排查性能问题的 AI Agent，必须遵循本文档与已注册的项目技能。

---

## 1. 已注册项目技能 (Registered Project Skills)

在开发或修改引擎内核、插件模块、构建脚本及跨模块接口时，**必须第一时间读取并遵循以下项目技能**：

- **`id-cpp-coding-style`**（代码风格与命名规范，写任何 C++ 代码必读）
  - **主文件**：[`.cursor/skills/id-cpp-coding-style/SKILL.md`](.cursor/skills/id-cpp-coding-style/SKILL.md)
  - **核心要求**：Allman 风格大括号（`{` 独占一行）、`namespace` 内部缩进 4 空格、函数/方法与变量统一使用 `snake_case`（如 `get_instance()`, `type_id<T>()`）、类型/枚举使用 `PascalCase`、静态成员使用 `s_` 前缀、指针/引用符号紧贴类型左侧（`T&`）、命名空间结尾必须带 `// namespace XXX` 注释。

- **`id-engine-architecture`**（微内核与零开销插件架构规范）
  - **主文件**：[`.cursor/skills/id-engine-architecture/SKILL.md`](.cursor/skills/id-engine-architecture/SKILL.md)
  - **代码与构建参考模板**：[`.cursor/skills/id-engine-architecture/reference.md`](.cursor/skills/id-engine-architecture/reference.md)
  - **适用场景**：
    - 设计或修改微内核 `ID.Kernel`（全局内存分配器、TLS 帧分配器、服务注册表、插件 DAG 拓扑调度器、Bootstrap 自举钩子）。
    - 新增或修改任何引擎插件（如 `ID.Math`、`ID.Containers`、`ID.Log`、`ID.JobSystem`、`ID.ECS`、`ID.Physics`、`ID.Render`、`ID.Gameplay`）。
    - 编写或调整 `ID/CMakeLists.txt` 及各插件的构建目标（双模构建：Dev 动态库热重载 vs. Release 全程序静态内联 LTO/LTCG）。
    - 优化跨插件调用性能、SIMD 寄存器传参（`__vectorcall`）与面向数据设计（DOD `std::span` 批处理）。

---

## 2. 核心架构三大铁律 (Core Architectural Mandates)

1. **微内核架构（Microkernel Only）**：
   - `ID.Kernel` 只保留最小且必要的底层基石：插件 DAG 管理系统、全局统一无锁内存分配器（含 TLS 帧分配器）、编译期 `TypeID` / 固定槽位服务注册表、以及极简自举钩子（Bootstrap Sink）。
   - 严禁把具体业务逻辑或具体子系统塞入微内核。
2. **一切皆插件（Everything is a Plugin）与多后端可插拔兼容**：
   - 所有的底层与业务模块（数学 `Math`、容器 `Containers`、日志 `Log`、任务系统 `JobSystem`、数据底座 `ECS`、物理 `Physics`、渲染 `Render` 等）全部定义为插件，每个插件在 `IDPlugin/<PluginName>/` 下拥有独立的子目录、`CMakeLists.txt`、`include/` 与专属 `Core.hpp` 导出宏。
   - **一槽多后端与下游显式兼容（方式 B）**：同一功能层面允许多个插件并存（如 `IDMath_SIMD` vs `IDMath_Fixed`、`IDRender_Vulkan` vs `IDRender_DX12`）。`id_add_plugin()` 自动向自身及所有下游公开注入 `ID_USE_PLUGIN_<UPPER_NAME>=1` 身份宏与 `ROLE_ALIAS` 角色别名；下游插件通过 `#if defined(ID_USE_PLUGIN_XXX) ... #elif ... #else static_assert(false)` 针对不同上游插件发挥各自独门优势，并在 `PluginDependency::compatible_ids` 中声明兼容白名单，不兼容的组合在编译期与微内核组装期双重拦截。
   - 插件生命周期严格遵循微内核拓扑排序对称四阶段：正序 `on_register -> on_init`，逆序 `on_shutdown -> on_unload`。
3. **极致运行效率优先（Zero Runtime Overhead over Compile Time）**：
   - **不怕编译时间长，想方设法把运行时开销降到最低**。
   - **物理分层 + 双模构建**：高频基础插件（`Math`、`Containers`）采用 C++20 Module / Header-Only 强制内联；系统插件采用 `id_add_plugin()` 双模构建——开发态支持 DLL 热重载，发布态（`ID_MONOLITHIC_BUILD=ON`）全部转为静态库并开启 `/GL` + `/LTCG`（或 `-flto=full`）实现跨所有插件的全程序内联优化。
   - **编码红线**：
     - 全局禁用 RTTI（`/GR-` / `-fno-rtti`）与异常（`/EHs-c-` / `-fno-exceptions`）。
     - 严禁带有运行时逻辑的全局/静态构造函数，全局状态必须使用 `constexpr` 或 `constinit`。
     - 严禁跨插件使用 C++ 虚表（`vtable`）或运行时字符串查表；必须使用编译期槽位 + 扁平函数指针表（或静态编译期直连）+ `std::span` 连续内存批处理。
     - 所有动态库插件必须链接 `IDPluginStub` 以将 `new`/`delete` 统一路由至微内核内存分配器，严禁跨 CRT 堆释放。

---

## 3. 三层平级目录结构与构建约定 (Three-Tier Peer Directory Layout)

整个仓库采用 **`ID`（微内核底座） + `IDPlugin`（插件大本营与植入桩） + `Sandbox`（测试与游戏宿主）** 的顶层三层平级物理隔离架构：

```text
IDGameEngine/
├── CMakeLists.txt                   # 顶层构建入口：依次编排 ID -> IDPlugin -> Sandbox
├── ID/                              # 【第一层：纯粹微内核仓库】仅产出微内核库 ID (.dll / .a)
│   ├── CMakeLists.txt               # 微内核编译配置，将 ID/include 暴露为 PUBLIC 头文件目录
│   ├── include/
│   │   ├── Common/                  # 零状态编译期基础设施 (Core.hpp, templates/Singleton.hpp, tools/LinearArena.hpp, tools/SlabSpan.hpp)
│   │   └── Kernel/                  # 微内核核心接口 (CoreTypes.hpp, BootstrapLog.hpp, Memory.hpp, Plugin.hpp)
│   └── src/
│       └── Kernel/                  # 微内核核心实现 (BootstrapLog.cpp, Memory.cpp, PluginManager.cpp)
│
├── IDPlugin/                        # 【第二层：插件大本营】管理 IDPluginStub 及所有独立自治插件子工程
│   ├── CMakeLists.txt               # 构建 IDPluginStub，定义 id_add_plugin()（含自动注入 ID_USE_PLUGIN_XXX 与 ROLE_ALIAS），按配置挂载子插件
│   ├── Stub/
│   │   └── IDPluginStub.cpp         # 每个插件强制私链的全局 operator new/delete 劫持桩（路由至 ID::Memory）
│   └── <PluginName>/                # 每个插件独立自治文件夹（如 IDMath/, IDLog/, IDECS/, IDPhysics/）
│       ├── CMakeLists.txt           # 插件自身构建脚本，调用 id_add_plugin()
│       ├── include/
│       │   └── <PluginName>/        # 插件自身头文件与专属 Core.hpp (定义 ID_<NAME>_API)
│       └── src/                     # 插件自身实现源码
│
├── Sandbox/                         # 【第三层：游戏宿主与冒烟测试】
│   ├── CMakeLists.txt               # 可执行程序目标，链接 ID + IDPluginStub + 所需插件
│   └── src/
│       └── main.cpp                 # 宿主入口与微内核/插件闭环测试
│
└── bin/                             # 构建产物统一输出目录（已配置在 .gitignore 中忽略，切勿提交二进制产物）
```
