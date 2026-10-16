---
name: id-cpp-coding-style
description: Apply IDGameEngine C++20 formatting, naming, compact Allman braces, aligned members, and Chinese documentation conventions when a programming SubAgent writes or reviews C++, modules, tests, or related CMake. Load explicitly from the SubAgent prompt; the main coordinating Agent need not load implementation formatting rules.
---

# 编程 SubAgent 专属规范

将本文件作为编码规范的唯一维护源。所有编程 SubAgent 在源码编辑前显式读取；主 Agent 负责委派，不默认加载细节。`.cursor/skills/id-cpp-coding-style/SKILL.md` 仅保留兼容指向。继承原规范，包括本轮之前已有的函数注释约定。

## 1. 缩进、大括号和换行

- 使用 4 个空格，禁止 Tab；`namespace` 内部缩进一级。
- 使用紧凑 Allman：命名空间、类型、函数及控制语句的 `{` 独占一行。
- 保留空构造函数的例外：成员初始化列表结束后允许同行 `{ }`。
- 函数签名和调用在约 120–140 列以内时保持单行，不为了形式拆开每个参数。
- 成员声明的类型、名称、初始化值和尾注按列对齐；局部改动只整理相关声明，不格式化整份大文件。
- `public:`、`protected:`、`private:` 与所在 `class`/`struct` 对齐，成员再缩进一级。
- 位于函数或命名空间内的预处理指令与当前作用域对齐，分支内再缩进一级。

## 2. 命名和类型

| 内容 | 约定 |
|---|---|
| 命名空间 | `PascalCase`，如 `ID::Log` |
| 类型、枚举、类型别名 | `PascalCase` |
| 函数、方法、局部变量、参数 | `snake_case` |
| 静态成员 | `s_` + `snake_case` |
| 类成员 | `m_` + `snake_case`；POD 结构字段可直接 `snake_case` |
| 编译期常量 | `k` + `PascalCase` |
| 枚举值 | `PascalCase` |
| 宏 | `ID_` + `UPPER_SNAKE_CASE` |
| 指针和引用 | `*`/`&` 贴类型左侧，如 `T* ptr`、`T& value` |
| 函数指针 | 使用紧凑 `void(*)(size_t, size_t)` |

包含 `<cstdint>` 与 `<cstddef>`，通过 `CoreTypes.hpp` 的已有别名或标准限定名使用固定宽度整数。保留既有公开 C ABI 名称，例如 `ID_AlignedAlloc`；不要为统一命名擅自破坏接口。

## 3. 头文件和注释

- 头文件从 `#pragma once` 开始，空行后包含依赖；先标准库头，再空行，再项目头。
- 命名空间结尾必须附 `// namespace <Name>`。
- 使用简短中文注释，描述契约、非显然的逻辑和设计理由；不插入“现在修改/新增了”等操作旁白，不解释显而易见的赋值。
- 行注释使用 `//`；块注释使用 `/** ... */`，每个 `*` 后保留两个空格。
- 简单 getter 或单行包装器使用一行说明。
- 复杂、热点、有危险前置条件或大量调用方依赖的函数，在声明上方添加块注释；内联函数在定义上方添加，不在声明和定义重复同一块。
- 注释包含 `@brief`、参数约束和返回值；`void` 省略返回说明。存在危险前提时添加 `@note`，说明破坏契约的后果。
- 注释必须与实现一致。禁止把没有执行的验证写成保证，也不能用注释代替输入检查或回滚逻辑。

## 4. 编辑纪律

- 遵循 `AGENTS.md` 的公共架构约束及所属子系统 Skill。
- 先读后改，精确局部替换；保留已有用户修改。失败匹配后重新读取对应区域。
- 不引入异常、RTTI、动态全局初始化或跨模块虚接口。普通 `operator new` 的声明需遵循标准分配契约，不能为“所有函数 noexcept”机械改写。
- 对公共 ABI、内存所有权和并发协议的修改同时补充契约与相应验证。
- 将诊断、构建和运行结果如实交回主 Agent；无关旧问题不借机重构。
