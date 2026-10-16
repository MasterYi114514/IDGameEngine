---
name: id-cpp-coding-style
description: Locate the canonical implementation-only coding conventions for IDGameEngine programming SubAgents. This compatibility entry redirects to .codebuddy/subagent-skills/id-cpp-coding-style/SKILL.md; the main coordinating Agent delegates programming instead of loading formatting details.
---

# 编码规范兼容入口

主 Agent 读取根 `AGENTS.md` 和 `.codebuddy/skills/id-subagent-workflow/SKILL.md`，将编程任务交给对应 SubAgent，不默认加载格式细节。

编程 SubAgent 在编辑前显式读取工作区根下 `.codebuddy/subagent-skills/id-cpp-coding-style/SKILL.md`。该文件是唯一规范维护源，已包含原紧凑 Allman、成员列对齐、命名、头文件和复杂函数注释约定。

本入口保留旧路径兼容性，不再复制规范正文；专属目录是按需加载约定，不是文件权限隔离。
