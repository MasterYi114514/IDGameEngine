---
name: id-subagent-workflow
description: Coordinate registration, task routing, scoped handoffs, and acceptance of IDGameEngine project SubAgents. Apply when registering a specialist, delegating any programming task, or maintaining AGENTS.md and task handoff documents. Keep implementation-only skills out of the main Agent context.
---

# SubAgent 注册与委派流程

## 1. 划分职责和知识

- 读取工作区根目录 `AGENTS.md`，确定公共架构、任务归属和已注册 Agent。
- 由主 Agent 负责需求澄清、任务拆分、范围授权、跨模块决策、交接与验收；将所有源码、测试代码和构建脚本编写委派给相应编程 SubAgent。
- 将公共架构与接口决策保留在 `AGENTS.md` 或任务的共享摘要中。不要把主 Agent 的上下文重复装入完整实现细节。
- 将格式、命名、注释和子系统内部协议放入 `.codebuddy/subagent-skills/`。由 Agent 系统提示正文指定并显式读取。这是按需加载约定，不是安全隔离或权限控制。
- 将长期可复用的规范写入 Skill，将阶段性实现计划和证据写入 `docs/`，将本次要求写入交接文档；避免三处复制同一份问题清单。

## 2. 注册一个 Agent

1. 确定名称、专长、触发条件、默认写入范围、可选协作范围、必读 Skill 和结果格式。不要让单个 Agent 自动包揽未授权模块。
2. 在 `.codebuddy/agents/<agent-name>.md` 创建项目级原生定义。采用 YAML front matter：`name`、`description`、`agentMode: agentic`、`enabled: true`、`enabledAutoRun: false`；将系统提示词放在正文。
3. 在正文列出角色、加载顺序、相对工作区的 Skill 路径、任务文档路径、写入边界、批准要求和验收责任。不要依赖主会话历史或自动继承 Skills。
4. 只有确认当前 IDE 支持的名称时才设置可选 `model`、`tools` 等字段。不要编造 `skills` 字段或默认继承行为。
5. 在根 `AGENTS.md` 的注册表中登记任务触发、定义路径、Skill 路径与范围。原生定义负责 IDE 发现，注册表负责项目路由；只更新注册表不等于安装 Agent。
6. 校验 front matter、文档链接与范围一致性；通过 IDE 的项目 Agent 设置检查发现情况并做一次只读加载验证。区分文件已登记与运行时已验证。

官方依据：`https://www.codebuddy.ai/docs/zh/ide/Features/Subagents`。官方允许直接创建 `.codebuddy/agents/` Markdown；未明确新增文件的刷新行为。未被发现时检查设置页 Agent Tab 的项目列表，必要时重新打开会话；不要承诺固定刷新命令。

## 3. 选择交接方式

- 对单文件、单目标、少量背景的任务，直接在委派消息里写目标、范围、必读路径和验收条件；不要为了形式创建文档。
- 对跨文件、失败路径、并发协议、接口变更或多轮任务，在 `docs/handoffs/<task-id>.md` 创建交接文档。按 `references/handoff-template.md` 填写。
- 只向 SubAgent 发送简短自足的委派消息：执行模式、目标、交接文档绝对路径、允许编辑路径、禁止事项、预期返回。详细证据留在文件里，不再复制进消息。
- 交接前记录当前提交与工作区差异，标明现有用户修改；文档中的源码行号仅作快照，执行前重新核验相关符号。
- 固定任务编号，随状态变化更新同一份交接，保留结果与阻塞项；不要无限新增聊天历史转录。

## 4. 派发与并发

1. 根据注册表选择 Agent；当前 `ID/**` 编程归 `id-kernel`。其他模块没有对应编程 Agent 时，先注册对应专家，不由主 Agent 代写。
2. 在派发消息中明确 `只读评审`、`制定计划` 或 `实施代码`。文档里的待办清单不自动授予实施权限。
3. 使用当前环境原生且允许的 Agent 调用机制。支持自定义 Agent 时使用实际注册名称与定义路径；调用器没有暴露该 Agent 时，明确说明需要 IDE 发现/新会话，不伪装成内置 Agent，也不谎称已经运行。
4. 向每个 Agent 分配互不重叠的写入集合。公共头、顶层 CMake 和同一份交接文档由一个任务串行持有；不要让主 Agent 与 SubAgent 同时修改。
5. 接到阻塞或需要扩大范围时，由主 Agent 更新交接/授权。Agent 不得借此越过用户拒绝、提交代码或处理工作区外文件。
6. 异步派发必须等实际结果；启动确认不是完成。不要把另一 Agent 的未返回结果写成事实。

## 5. 验收与沉淀

- 要求返回：状态、修改文件、公共接口变化、实际执行的验证及结果、未验证项、阻塞项、下一步。
- 主 Agent 复核需求和范围，检查差异及验证证据；发现实现问题时交回 SubAgent 修复，不直接代写编程代码。
- 由主 Agent 更新公共摘要和交接状态；由实现 SubAgent 在获准范围内更新实施结果和专属参考。并发时分时写入。
- 只把稳定新规则提炼入 Skill；缺陷状态与测试结果留在项目计划。没有测试证据不得把静态推导标为运行复现。
- 不自动提交、推送、打包 Skill 或清理 `.codebuddy/`。项目原位安装只需文件；若明确需要分发包，再使用 Skill 打包工具。

## 当前注册

- `id-kernel`：`.codebuddy/agents/id-kernel.md`；内核开发 Skill 为 `.codebuddy/subagent-skills/id-kernel-development/SKILL.md`。
- 编程规范：`.codebuddy/subagent-skills/id-cpp-coding-style/SKILL.md`，所有编程 SubAgent 共用，主 Agent 不按惯例加载。
- 内核待修计划：`docs/subagents/id-kernel-plan.md`，仅 `id-kernel` 按任务读取；共享状态见根 `AGENTS.md`。
