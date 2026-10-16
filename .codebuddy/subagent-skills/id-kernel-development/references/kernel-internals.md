# ID 内核内部参考（仅实现 SubAgent 按任务读取）

基线：2026-10-15 工作区静态评审。具体修复状态只维护在 `docs/subagents/id-kernel-plan.md`；本文保存模块定位和协议知识。源码及已确认的新决策优先于快照，发现矛盾先报告，不照抄模板。

## 1. 源码地图

| 领域 | 文件 |
|---|---|
| 平台宏、导出/内联/调用约定 | `ID/include/Common/Core.hpp` |
| 基础整数、哈希、TypeID | `ID/include/Kernel/CoreTypes.hpp` |
| 单例模板 | `ID/include/Common/templates/Singleton.hpp` |
| Slab 页面和空闲链表 | `ID/include/Common/tools/SlabSpan.hpp` |
| 帧线性分配器 | `ID/include/Common/tools/LinearArena.hpp` |
| 内存对外接口、档位和统计 | `ID/include/Kernel/Memory.hpp` |
| TLS 调度、大对象、OS 映射 | `ID/src/Kernel/Memory.cpp` |
| 插件描述符、依赖和服务槽 | `ID/include/Kernel/Plugin.hpp` |
| 拓扑与生命周期 | `ID/src/Kernel/PluginManager.cpp` |
| 自举日志与断言 | `ID/include/Kernel/BootstrapLog.hpp`、`ID/src/Kernel/BootstrapLog.cpp` |
| C++ 全局分配桩 | `IDPlugin/Stub/IDPluginStub.cpp`（协作范围） |
| 演示与微基准 | `Sandbox/src/main.cpp`（协作范围） |
| 构建 | 根、`ID/`、`IDPlugin/`、`Sandbox/` 的 `CMakeLists.txt` |

## 2. 内存协议

### Slab 与 TLS

- 每个线程维护 15 个档位；小对象上限 8KB，页面 64KB，`SlabSpan` 头占 128B。实际常量在 `Memory.hpp` 和 `SlabSpan.hpp`。
- `m_slab_heads` 永远表示完整页链，`m_active_slabs` 只是快路径缓存。慢路径寻找本地空闲或摘远端链，最后才向 OS 申请新页。
- 所属线程访问 `local_free_list`；跨线程只 CAS 挂入 `remote_free_list`。跨线程会读取不可变归属字段，因此缓存行分离不是绝对“没有任何共享流量”。
- 生产者先写自己的 `next`，release CAS 发布；消费者 acquire exchange 摘整链。生产者不解引用旧 head；单消费者不会用旧 next 做 CAS 弹栈，因此经典 ABA 丢链不直接成立。
- 如加入页解除映射、归属转移、多消费者或新计数器，重新证明发布、接管、在途操作和回收协议。TLS 地址不能被当成跨线程世代永久稳定身份。
- 空闲链表采用侵入式节点，块大小必须满足节点大小及对齐。对非 2 幂档位，用能够整除步长的 2 幂对齐，不用 `~(blk_size - 1)`。

### 大对象

- 普通大对象元数据位于用户指针所在 64KB 页首；当用户指针本身位于页边界时，头位于前页。头保存 OS 基址、分配字节和用户偏移。
- 区分页首指针和页内指针后再定位头；不能把用户第一页的数据探测为 Slab 或 Large header。Magic 仅辅助检查分配器拥有的元数据。
- 对请求加头部/对齐预留、最终页面取整及 OS 附加映射都做 checked arithmetic；容量验证使用已证安全的偏移/剩余量。
- 释放必须还给原始映射；POSIX 可以保存原区间，或在对齐后正确解除前后缀。不能只对内部指针解除请求长度。
- `realloc` 失败保持原块；成功复制不超出已确认容量，并按接口约定释放原块。

### Arena、对象生命周期与失败

- 默认帧 arena 为线程私有 4MB；`reset` 只回退游标，不调用用户对象析构。用户必须保证帧引用和对象生命周期结束。
- 检查 alignment、填充、剩余容量、数组乘法和未初始化状态；失败不得改变游标。
- 对 OS 原始存储显式构造 metadata，特别是 atomic；对复用块审查用户对象到 `SlabFreeNode` 的生命周期转换。不要把 OS 清零等同于 C++ 对象构造。
- 分配器接口可以返回空；普通 C++ new 使用不可恢复失败策略，只有 nothrow new 返回空。紧急故障输出不得再次调用可能失败的分配器。

## 3. 插件、槽位和生命周期

- 最大插件数 128，服务槽 64；`MaxSlots` 是边界哨兵，不是合法下标。
- `PluginDependency` 支持精确 ID 或兼容 ID 白名单，以及最低版本。`slot` 当前主要用于诊断，不自动绑定提供者。
- 当前每插件最多 32 条依赖，用 bit 标识满足状态。每条依赖只增减一次，同一上游可满足多条重复依赖。
- OR 可达性排序不是固定选择某候选后的普通图。若引入后端选择，不能简单选注册顺序第一个候选后把原本可满足的 OR 图错误判环；需要明确选择规则和稳定性。
- 正序全部 register 再 init，逆序全部 shutdown 再 unload。分别记录两阶段完成集合；init 失败时关闭成功前缀、卸载完整已注册集合。
- 明确失败插件自行清理部分初始化资源的契约，或另行设计失败清理回调；不能对未就绪插件盲目调用普通 shutdown。
- 通过状态机拒绝初始化/关闭重入及回调期间注册新插件。服务发布是阶段操作，不授予结构性重入权限。
- 单槽只有一个指针；明确活动提供者、API 身份及所有权，不同表不能静默覆盖。类型/版本检查放在冷路径，关闭与失败回滚兜底清空。
- 注册后描述符和嵌套 span 必须稳定存活且不变；若需要动态描述符，冻结/复制机制必须覆盖嵌套数据与 DLL 驻留。

## 4. 日志、TypeID 与 ABI

- Bootstrap 保持对日志插件零编译依赖；sink 是原子函数指针。release 发布与 acquire 读取配对才能发布新 sink 依赖的状态。
- acquire 不是旧 DLL 卸载屏障。安全热重载必须处理取得旧地址的在途调用、缓存指针和模块驻留。
- 早期日志缓冲是设计目标；若实现固定缓冲，明确容量、溢出和并发回放规则，不在 OOM 路径动态分配。
- `type_id<T>()` 哈希编译器函数签名；同一受控构建可用，但不作为跨工具链、存档或 ABI 布局身份。公开持久身份使用显式 ID 并单独版本化布局。
- 跨 DLL 接口需约束工具链、CRT 和调用约定。引入 `abi_version`、`struct_size` 等握手后再谈版本兼容；仅 `extern "C"` 不会稳定含 C++ 类型的布局。

## 5. 构建与测量

- 三层编排为 `ID -> IDPlugin -> Sandbox`。`ID_MONOLITHIC_BUILD` 切 STATIC/SHARED；Release 不自动打开该选项。IPO 只表达优化请求，不证明调用内联。
- `id_add_plugin` 提供身份宏、角色别名及 Stub 链接原语；真实子工程、后端选择和动态模块管理必须另行实现。
- `IDPluginStub` 统一主要 new/delete，但普通 STATIC 链接不应未经验证就宣称每个 DLL 都强制植入；也不覆盖任意第三方 malloc、类级分配函数。
- 当前共享源码树输出可能使 OFF/ON 同配置产物互相覆盖；新增验证需先规划互不混淆的产物位置。
- AVX2 与 fast-math 需明确支持范围与数值契约；不要把后端 CPUID 当成已经 AVX2 编译的整个内核的保护。
- 微基准测量需关注优化消除、真实 DLL 边界、碎片、峰值保留量、慢路径比例与延迟分布。不要用短循环或 `ALL PASSED` 证明端到端无锁、实时有界或零开销。
