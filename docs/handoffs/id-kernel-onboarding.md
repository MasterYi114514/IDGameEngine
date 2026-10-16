# ID 内核 SubAgent 首次交接：配置加载验证

## 元信息

- 任务编号：id-kernel-onboarding。
- 执行 Agent：`id-kernel`。
- 执行模式：只读加载验证，不实施 C++ 修复。
- 状态：ready；原生自定义 Agent 的实际发现/调用待 IDE 验证。
- 日期：2026-10-15。
- 源码基线：当前工作区有既有未提交改动，涉及旧编码 Skill、SlabSpan、Memory/Plugin 头与实现。不得回退或归因于本轮源码修复。

## 共享摘要

用户要求将后续编程委派给按领域注册的 SubAgent。本轮登记第一个 `id-kernel`，主 Agent 保留公共架构、任务授权和验收；实现者读取专属格式与内核协议。

允许本次 Agent 读取工作区相关文件；禁止编辑源码、执行构建、提交或清理。若实际调用成功且交接允许更新结果，可只写本文“结果”区；本轮配置审阅先按完全只读处理。

通过标准：实际读到必读路径，理解默认/协作范围，识别计划是静态快照而非全量实施授权，返回阻塞与矛盾项。加载通过不等于代码修复或测试通过。

## 加载清单

1. `AGENTS.md`。
2. `.codebuddy/agents/id-kernel.md` 的角色与范围。
3. `.codebuddy/subagent-skills/id-cpp-coding-style/SKILL.md`。
4. `.codebuddy/subagent-skills/id-kernel-development/SKILL.md`。
5. `.codebuddy/subagent-skills/id-kernel-development/references/kernel-internals.md` 的源码地图及内存/生命周期相关部分。
6. `docs/subagents/id-kernel-plan.md` 的状态表和 K1 工作包。

不要默认加载 `docs/id-math-plugin-plan.md` 或架构模板。

## 委派要求

仅确认路径加载、任务边界和首个 K1 的目标/风险，返回简短报告，不复述全部实施规范。若 IDE 当前调用器没有暴露 `id-kernel`，由主 Agent 明确报告未进行原生调用，不以其他内置 Agent 的结果充当该验证。

## 结果

- 配置创建：已创建 `.codebuddy/agents/id-kernel.md` 和专属 Skill。
- 源码事实复核：内置只读 `code-explorer` 已复核计划基础事实；它不是 `id-kernel` 原生调用。
- 文件结构/链接：内置只读审阅确认链接存在、职责和授权一致；三个新 Skill 通过官方基础校验，相关文件无 IDE 诊断，Git diff 格式检查通过。旧架构模板的失败契约及构建缺口已在文件顶部明确警告；历史代码示例未改。
- 编码规范迁移：保留原缩进、Allman 例外、单行签名、列对齐、命名、头文件分组和复杂函数注释要求；旧入口转为兼容指向，不再双份维护。
- 原生发现/调用：未验证。当前会话可调用列表未列出新 `id-kernel`，不得声称实际运行通过。
- C++ 实施与运行测试：未执行，本任务不授权。

## 后续

在 IDE 设置页 Agent Tab 的项目列表确认 `id-kernel`；若当前会话未发现，检查文件/目录和会话加载情况后重新验证。官方文档未规定刷新命令。运行时加载验证成功后，另建明确授权的 K1 实施交接。
