# 引擎实施架构参考（编程 SubAgent 按需读取）

由原 `.cursor/skills/id-engine-architecture/SKILL.md` 的实施规则整理。公共架构维护在根 `AGENTS.md`；本文描述实现方式与目标，不把尚未落地的能力写成既有保证。

## 1. 架构红线

- 保持微内核只包含插件 DAG、统一分配器/TLS arena、TypeID/固定服务槽和自举钩子。业务模块独立作为插件。
- 禁用 RTTI 与异常，使用 `/GR- /EHs-c-` 或 `-fno-rtti -fno-exceptions`；采用显式失败契约。`type_id<T>()` 使用 consteval 哈希，但不等于稳定 ABI ID。
- 全局及静态状态采用 `constexpr`/`constinit`，不允许运行期全局构造；线程资源采用显式生命周期管理。
- 不跨插件暴露虚接口；动态模式采用扁平函数表，静态模式采用可被 LTO 优化的直接声明与包装器。
- 热路径跨插件调用按连续批次组织，优先 `std::span`、SoA 和可证明的 restrict；避免逐对象跨 DLL 调用。
- 插件与宿主主要 C++ 分配经 `IDPluginStub` 路由内核，显式 allocator 同样统一归属。禁止混用堆释放；验证实际植入，不把 PRIVATE 链接声明当作充分证据。

## 2. 插件层级和双模构建

| 层级 | 示例 | 开发态目标 | 发布态目标 |
|---|---|---|---|
| Tier 0 | Kernel、PluginStub | 内核 DLL + 植入桩 | 静态内核与全程序优化 |
| Tier 1 | Math、Containers、Log 前端 | Header-only / C++20 Module | 内联基础运算 |
| Tier 2 | JobSystem、ECS、Physics、Render | 独立 DLL 与扁平 API | STATIC + LTO/LTCG |
| Tier 3 | Gameplay、工具逻辑 | 安全点可替换的逻辑 DLL | 静态构建与按需 PGO |

- 使用 `ID_MONOLITHIC_BUILD` 明确切换模式，配置 OFF/ON 不得共享混淆的产物。Release 与 monolithic 是两个维度。
- `/GL`、`/LTCG` 或工具链支持的 Full LTO 为优化目标；需验证最终产物，不能保证“全部内联”。
- 用合法的平台调用约定优化 SIMD 参数，例如支持时的 `__vectorcall`；不要向不支持的 GCC/平台机械展开。
- 将 AVX2/FMA 后端和基线支持范围分离，数值优化由具体模块选择，不能用 fast-math 的全局开关替代物理确定性决策。

## 3. 自治插件与方式 B

1. 在 `IDPlugin/<PluginName>/` 建立独立 `CMakeLists.txt`、`include/<PluginName>/Core.hpp` 和 `src/`。
2. 决定层级、角色别名、导出宏、插件 ID 和依赖白名单。
3. 用 `id_add_plugin()` 提供身份宏 `ID_USE_PLUGIN_<UPPER_NAME>=1` 和可选 `ROLE_ALIAS`。
4. 下游通过 `#if defined(...)` 适配选定后端，不强迫所有后端统一为最低能力接口。
5. 声明 `PluginDependency::compatible_ids` 与最低版本，运行时组装至少有一个合格候选。
6. 明确唯一活动槽提供者与选择规则，多后端可存在不等于允许共享槽的最后写覆盖。编译分支、依赖选择与运行时绑定必须一致。

生命周期保持四阶段：正序 register/init，逆序 shutdown/unload。register 发布服务或元数据，不允许借机重入组装；init 保证已选择依赖就绪；shutdown 停止任务和资源使用；unload 解绑。失败回滚需要分阶段记录，而不是只按成功 init 数卸载。

## 4. 自举日志和热重载目标

- Kernel 不依赖日志插件，由固定 fallback 和原子 sink 提供自举输出。
- 若提供早期回放，使用预分配的小型缓冲，定义容量、溢出和并发切换；当前是否已实现以源码为准。
- 日志和断言保持 cold/noinline，热点判断使用 unlikely；不可恢复错误使用不返回路径，不让调试器继续后执行非法逻辑。
- 业务持久状态优先留在 ECS 连续存储或显式状态容器，不让可卸载 DLL 的静态对象拥有世界状态；不得因此给微内核塞入具体业务。
- 热重载必须建立安全点、停止任务、在途调用退出和旧模块驻留协议。原子换指针不等于允许立即卸载旧 DLL。
- 公共 API 与描述符带版本化 ABI 契约；不能只用插件功能版本检测布局。

## 5. 模板与现状

原代码/CMake 模板保留在 `.cursor/skills/id-engine-architecture/reference.md`，仅在需要历史结构时读取。模板不是权威方案：普通 new 直接返回空的失败路径、Stub 必然植入的表述、共享输出及全目标 AVX2/fast-math 均不得照抄。符号、路径、生命周期和硬件约定应以当前源码、Skill 和已授权决策为准。

当前内核缺陷和证据只维护在 `docs/subagents/id-kernel-plan.md`；Math 的实施方案在 `docs/id-math-plugin-plan.md`，只有需要核对跨模块契约时才读取，并交给后续 Math 专职 Agent 实现。
