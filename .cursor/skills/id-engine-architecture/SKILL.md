---
name: id-engine-architecture
description: Locate shared IDGameEngine architecture in AGENTS.md and task-specific implementation references for programming SubAgents. This compatibility entry does not require the main coordinating Agent to load kernel implementation details.
---

# 架构规范兼容入口

读取根 `AGENTS.md` 获取微内核边界、插件分层、效率红线和 Agent 路由。主 Agent 只需公共约束和本次交接摘要。

编程 SubAgent 按任务显式读取：

- 内核实施：`.codebuddy/subagent-skills/id-kernel-development/SKILL.md`。
- 原架构实施规则：`.codebuddy/subagent-skills/id-kernel-development/references/engine-architecture.md`。
- 原代码/CMake 模板：`.cursor/skills/id-engine-architecture/reference.md`，保留原文件，按需使用。

将模板视为设计参考，不把 DLL 构建、IPO 或原子换表误认为已验证的安全热重载和全程序内联。其他模块的实现交给后续注册的相应编程 SubAgent。
