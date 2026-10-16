---
name: id-kernel-development
description: Guide the id-kernel SubAgent in implementing and reviewing IDGameEngine microkernel memory, plugin graph and lifecycle, service binding, TypeID, bootstrap failure paths, and build integration. Load before ID kernel programming, then read only task-relevant internal references and project repair-plan sections.
---

# ID 内核实施 Skill

## 职责与加载

负责 `ID/**` 的实现与验证。只在交接授权时触及分配桩、Sandbox、测试和共享构建脚本；不实现 Math、ECS、Render 等插件业务。

1. 读取根 `AGENTS.md` 的公共架构，及同级 `../id-cpp-coding-style/SKILL.md`。
2. 读取本次交接的模式、写入范围和验收标准。
3. 按任务读取 `references/kernel-internals.md` 对应章节，禁止把整套实现细节返回主 Agent。
4. 按任务读取 `docs/subagents/id-kernel-plan.md` 相应工作包。该计划是 2026-10-15 源码静态评审快照，先核实源码和差异，再实施。
5. 涉及跨插件或构建架构时按需读取 `references/engine-architecture.md` 与原 `.cursor/skills/id-engine-architecture/reference.md`。这些是设计约束/模板，不是现成功能证明。

## 1. 保持关键不变量

- 将 Slab 链表头与当前分配页缓存分离，不能因缓存移到中间而丢失历史页。
- 将生产者 CAS 入链、所属线程 `exchange` 摘整链视为当前 remote 协议；不套用 CAS 弹栈的经典 ABA 结论。单消费者、合法释放和页存活前提变化时重新证明。
- 对大对象使用分配器拥有的元数据识别布局，不读取用户可写内容作为类型权威。
- 对齐取整、地址偏移、字节数乘法全部检查；失败不得损坏原 arena 或 realloc 原块。
- 明确线程退出后的页归属与晚释放协议；禁止在仍有外部引用时解除映射。
- 每条 OR 兼容依赖只贡献一次入度，满足时独立记录；版本不足候选不得阻止其他达标候选。
- 将依赖选择和实际服务提供者绑定；槽位冲突不得通过最后写入隐式解决。
- 区分已注册与已初始化，完整逆序回滚；在回调前进入明确状态并拒绝重入。
- 使用无分配依赖的不可恢复故障路径；普通 new 失败不得返回空，nothrow 允许返回空。
- 以正确性为优化前提。日志发布同步不等于卸载安全；静态库/IPO 配置不等于已经验证内联；OS 慢路径不承诺端到端实时有界。

## 2. 分层实施

- 优先完成小而可验证的工作包：元数据识别 → 算术/OOM/断言 → 生命周期 → 服务选择 → 线程回收 → 跨 DLL 与性能。
- 在变更公共头、生命周期回调签名、后端选择或 ABI 前，把必要决策交给主 Agent。默认保留现有接口，不能为了套模板大面积重写。
- 在 OS 原始存储上明确建立元数据对象生命周期，尤其是 `std::atomic`；审查用户对象与侵入式空闲节点的类型切换。把标准层面风险与实际复现区分。
- 将检查尽可能放在注册、组装和慢路径；Debug 的类型/所有权诊断不得自动变成 Release 每次高频调用的重型开销。

## 3. 验证和回传

- 在授权范围内为修复添加针对性回归，不以 Sandbox 的 `ALL PASSED` 字符串替代覆盖证据。
- 覆盖合法用户数据、失败注入、极值运算、重复生命周期、多个兼容候选、多生产者与单消费者重叠、线程退出晚释放。
- 检查当前可用工具和配置。缺编译器或某平台不能运行时，报告阻塞；未执行测试保持“未验证”，不得写成通过。
- 区分 Windows 已支持路径与 POSIX 待修路径；区分动态构建、真实插件加载和安全热重载。
- 返回状态、文件、公共接口变化、实际验证、未验证与风险；获准时更新任务结果和计划对应项。不要把稳定 Skill 写成不断增长的聊天记录。
