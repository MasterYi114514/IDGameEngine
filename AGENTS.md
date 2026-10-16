# AGENTS.md — IDGameEngine AI 开发指南

欢迎来到 **IDGameEngine**（纯自研 C++20 高性能微内核游戏引擎）。任何在此仓库中进行架构设计、编写代码、修改 CMake 或排查性能问题的 AI Agent，必须遵循本文档与已注册的项目技能。

---

## 1. Agent 分工与注册入口

**主 Agent 负责需求、拆分、公共接口决策、交接与验收；后续所有源码、测试代码和构建脚本编写交给对应编程 SubAgent。** 主 Agent 可维护流程与项目文档，不代写编程实现。未注册的模块先注册对应专家，不把插件业务交给内核 Agent。

### 1.1 主 Agent 必读

- 本文件：公共架构、领域归属和共享状态。
- [`id-subagent-workflow`](.codebuddy/skills/id-subagent-workflow/SKILL.md)：注册、任务路由、交接、权限与验收流程；在注册或委派时加载。
- 本次任务的共享摘要；复杂任务放在 `docs/handoffs/`，简单任务直接交接。

### 1.2 已注册 SubAgent

| 名称 | 必须委派的任务 | 原生定义 | 默认领域 / 协作边界 |
|---|---|---|---|
| `id-kernel` | `ID` 内核 C++、内存、插件 DAG/生命周期、服务槽、TypeID、自举日志及内核构建实现 | [`.codebuddy/agents/id-kernel.md`](.codebuddy/agents/id-kernel.md) | 默认 `ID/**`；Stub、Sandbox、测试和共享 CMake 仅在交接明确授权时编辑；不实现插件业务 |

每次派发明确目标、只读/计划/实施模式、允许写入路径和验收条件。Agent 定义和路线图不自动授权实施全部待办。不得让多个 Agent 并发修改同一文件。

### 1.3 仅编程 SubAgent 按需读取

- 编码规范唯一维护源：[`.codebuddy/subagent-skills/id-cpp-coding-style/SKILL.md`](.codebuddy/subagent-skills/id-cpp-coding-style/SKILL.md)。缩进、命名与注释细节不在本文件重复。
- 内核实施 Skill：[`.codebuddy/subagent-skills/id-kernel-development/SKILL.md`](.codebuddy/subagent-skills/id-kernel-development/SKILL.md)，由 `id-kernel` 系统提示正文显式指定。
- 内核内部参考与阶段计划：[内部参考](.codebuddy/subagent-skills/id-kernel-development/references/kernel-internals.md)、[`docs/subagents/id-kernel-plan.md`](docs/subagents/id-kernel-plan.md)，只读取本次工作包相关章节。
- 旧 `.cursor/skills/` 入口只保留兼容指向；架构模板仍保留在原路径。不要再维护两份编码规范。

`.codebuddy/subagent-skills/` 是专属按需加载目录，不依赖默认 Skill 自动发现，不是文件权限隔离。主 Agent 默认不加载实现细节；确需核验时可读取相关部分。

### 1.4 共享状态与完成口径

2026-10-15：内核仍为原型；Slab 链表头、OR 依赖计数和版本筛选已有修复，优先处理内存识别/失败契约、生命周期与服务绑定，再做资源回收和真实 DLL 验证。本轮建立流程和文档，不声称已实施剩余代码修复；详细证据由内核计划维护。

原生 Agent 文件已经登记；运行时发现与调用需在当前 IDE 验证，不能把配置文件存在等同于调用通过。Skill 中的路径不代表系统自动继承。

---

## 2. 核心架构三大铁律 (Core Architectural Mandates)

1. **微内核架构（Microkernel Only）**：
   - `ID.Kernel` 只保留最小且必要的底层基石：插件 DAG 管理系统、全局统一内存分配器（含线程私有快路径、跨线程无锁归还与 TLS 帧分配器）、编译期 `TypeID` / 固定槽位服务注册表、以及极简自举钩子（Bootstrap Sink）。OS 与日志慢路径不承诺端到端无锁或实时有界。
   - 严禁把具体业务逻辑或具体子系统塞入微内核。
2. **一切皆插件（Everything is a Plugin）与多后端可插拔兼容**：
   - 所有的底层与业务模块（数学 `Math`、容器 `Containers`、日志 `Log`、任务系统 `JobSystem`、数据底座 `ECS`、物理 `Physics`、渲染 `Render` 等）全部定义为插件，每个插件在 `IDPlugin/<PluginName>/` 下拥有独立的子目录、`CMakeLists.txt`、`include/` 与专属 `Core.hpp` 导出宏。
   - **多后端与下游显式兼容（方式 B）**：同一功能层面允许多个插件实现存在（如 `IDMath_SIMD` vs `IDMath_Fixed`、`IDRender_Vulkan` vs `IDRender_DX12`）。通过身份宏、角色别名和兼容白名单明确选择与适配；编译分支、组装依赖和实际服务提供者必须一致。单个全局槽的后写覆盖不能作为多后端选择规则，具体策略由任务确认后实施。
   - 插件生命周期保持正序 `on_register -> on_init`、逆序 `on_shutdown -> on_unload`；失败回滚应完整覆盖已经发生的注册副作用，不能只保证成功路径对称。
3. **极致运行效率优先（Zero Runtime Overhead over Compile Time）**：
   - **不怕编译时间长，在正确性和可验证的生命周期前提下尽量降低运行时开销**。
   - **物理分层 + 双模构建**：高频基础插件（`Math`、`Containers`）采用 C++20 Module / Header-Only；系统插件开发态采用 DLL，发布态（`ID_MONOLITHIC_BUILD=ON`）采用静态库与 LTO/LTCG。安全热重载和跨插件内联是需要实施与验证的目标，不能仅凭 SHARED/IPO 配置声称完成。
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
