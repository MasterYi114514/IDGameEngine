# Sandbox：游戏本体宿主（产品消费者说明）

本目录是第五层里的"游戏本体"，也是产品的**真实消费者**：

- 只经产品消费目标 `IDGameEngine` 与 `ID::Engine::start()` / `ID::Engine::shutdown()` 消费产品；
  不登记插件描述符、不手工安装插件、不维护第二份依赖表（插件集合只由构建期 profile 决定）。
- 不承担任何正确性测试或性能验证职责：测试与测量全部归 `Benchmark`，本目录没有、也不应该有测试代码。
- 生命周期输出固定两行（stdout）：`[Sandbox] product started (state=Ready)` / `[Sandbox] product stopped (state=Idle)`；
  失败时打印 `code` / `state` / `slot` 并返回 `1`。日志行（含 `ID.Log` 的 `attached` / `ready` / `detached`）走 stderr。

## 1. 构建与运行（可复制）

日常开发（preset `dev` = profile `default` + SHARED + Debug，本机继承名 `dev-mingw`）：

```powershell
cmake --preset dev-mingw
cmake --build F:\IDGameEngine\build\dev --target Sandbox --parallel
& F:\IDGameEngine\build\dev\out\shared\bin\Sandbox.exe; "ExitCode=$LASTEXITCODE"
```

交付 profile `ship`（`Assemble/profiles/ship.json`）的验证构建（`-B` 用 `Benchmark\build\<变体>`，
不占用工程根 `build\`，也不与其它构建目录互相覆盖）：

```powershell
cmake -S F:\IDGameEngine -B F:\IDGameEngine\Benchmark\build\ship-profile-l02 -G "MinGW Makefiles" `
      -DCMAKE_BUILD_TYPE=Release -DID_ASSEMBLY_PROFILE=ship -DID_BUILD_SANDBOX=ON `
      -DID_BUILD_BENCHMARK=OFF -DID_BUILD_ASSEMBLY_SMOKE=OFF
cmake --build F:\IDGameEngine\Benchmark\build\ship-profile-l02 --target Sandbox --parallel
& F:\IDGameEngine\Benchmark\build\ship-profile-l02\out\shared\bin\Sandbox.exe; "ExitCode=$LASTEXITCODE"
```

产物位置由构建模式决定：`<build>\out\shared\bin`（默认 SHARED）或 `<build>\out\static\bin`
（`ID_MONOLITHIC_BUILD=ON`）。`ID_BUILD_SANDBOX=ON` 必须同时给 `ID_ASSEMBLY_PROFILE`，否则在
`Sandbox/CMakeLists.txt` 配置期直接 `FATAL_ERROR`。

最小消费形态就是 `src/main.cpp`：`ID::Engine::start()` → 检查返回值（失败打印 `code` / `state` / `slot`
并返回非 0）→ 业务 → `ID::Engine::shutdown()`。宿主不得自己创建插件、不得静态构造自动启动，
也不要在 `start()` 之前假设任何服务槽可用。

> 现状：公共 preset `ship` 的 `ID_ASSEMBLY_PROFILE` 仍是 `default`（fixture 产品），尚未切到 `ship`
> profile。切哪个 profile 属于产品选择与构建配置（`CMakePresets.json`），不在 Sandbox 层，本层只消费结果。

## 2. 产品 profile 与 `enabled_plugins`

`ship` 的选中集合（显式列出，解析器**不自动补依赖**）：

| 插件 | 形态 | 说明 |
|---|---|---|
| `ID.Log` | `module` + `standard`，槽位 0，接口修订 2 | 唯一日志能力提供者；无依赖 |
| `ID.Math.Scalar` | `interface` + `none`，无槽位 | 产品发布的数学 backend；无依赖、无运行期产物 |

不选 `ID.Math.SIMD` 的理由是与 ISA 基线有关：同时选中两个 backend 的产品整体按 SIMD 侧
（x64 SSE2，编译期 gate）的最低 ISA 发布，只有"仅 Scalar"的产品才承诺更低基线。
`A only` / `B only` / `A+B` 三种组合属于消费验证矩阵，需要各自独立构建单独取证，不在 `ship` 里合并。
本层对这三种组合**未取证**：当前没有任何记录表明它们的宿主被运行过、也没有对应的退出码产物，
因此不得写成已支持；取证属于第 6 节待测项。

## 3. 日志：当前生效的默认值

`ID.Log` 在自己的 `on_init` 用**默认 `LogConfig{}`** 建后端（`IDPlugin/IDLog/src/IDLogModule.cpp`
的 `IDLogOnInit`），宿主目前没有任何覆盖点。因此凡选中 `ID.Log` 的产品（例如 `ship`）都只有下面这套默认值：

> 注意 profile 差异：`dev` preset 用的是 profile `default`（只选 fixture 插件，**不含 `ID.Log`**），
> 那种产品的日志只走内核自举 sink（stderr；Windows 下同时调 `OutputDebugStringA`），
> 没有 `ID.Log` 的 `attached` / `ready` / `detached` 行。
> 需要日志能力时用选中 `ID.Log` 的 profile（`log` / `ship`）。

下表的取值与 `LogConfig` 的默认值逐项对应：

| 项 | 当前值 | 含义 |
|---|---|---|
| `min_level` | `Info` | 唯一过滤真源 |
| `enable_console` | `true` | 写 stderr（pattern 不含颜色标记，输出不着色） |
| `enable_file` | `false` | **没有文件 sink**，`file_path` 全 0 |
| `async` | `true` | 异步形态，队列满时丢最旧一条 |
| `queue_size` | `8192` | 异步队列容量（只在 `async` 时校验，须非 0 且为 2 的幂） |
| `flush_interval_ms` | `0` | 不起周期 flush |
| `flush_on_warn` | `true` | warn 及以上立即 flush |
| `max_file_bytes` / `max_files` | `0` / `0` | **轮转未启用**（两者同时非 0 才启用） |

## 4. 启动期配置（L02）现状：**未实现（阻塞）**

目标行为是"产品启动配置在 `ID.Log::on_init` **之前**生效"，否则下游在自己的 `on_init` 里写的
启动期文件日志会丢失。当前不存在这样的注入点，四条证据（源码位置）：

1. `Assemble/src/EngineEntry.cpp` 的 `ID::Engine::start()` 只有一句
   `PluginRuntime::instance().start(*Assembly::Generated::ID_AssemblyPlanView())`；
   `ID/src/Kernel/PluginRuntime.cpp` 的 `start()` 在**同一次调用内**先跑完全部 `on_register`
   再跑全部 `on_init`，宿主没有可插入的中间点。
2. 进入 `start()` 之前，服务槽表是空的（`ID/include/Kernel/PluginRuntime.hpp` 里
   `m_service_slots` 初值全为 `nullptr`，只有 `on_register` 才会发布），所以宿主在 `start()` 之前
   连 `ID.Log` 的函数表都取不到。
3. `LogApiTable::configure` 只接受 `min_level` / `flush_interval_ms`，`reconfigure` 接受全部字段，
   但两者都要求插件后端已建立：后端建立之前，前端实现这两个入口时一律记
   `LogConfigError::NotReady` 并返回 `false`（`IDPlugin/IDLog/src/SpdlogFrontend.cpp`）。
4. `IDPlugin/IDLog/src/IDLogModule.cpp` 的 `IDLogOnInit` 固定构造 `LogConfig config{}`，
   不使用任何宿主输入；公开头 `IDLogApi.hpp` 也没有"预置启动配置"入口。

**因此本层不做启动期配置，也不允许用 `start()` 之后调 `reconfigure` 冒充它**：那条路径只能改
已经建好的后端，`on_init` 阶段写出的日志早已按默认配置输出，正是必须避免的丢失路径。

解除阻塞需要产品层注入点（不属于本层实现）：`ID::Engine::start()` 接受一个"register 之后、
init 之前"执行一次的宿主钩子（由 Assemble 产品入口与内核 `PluginRuntime` 一侧提供），
同时 `ID.Log` 一侧需要能在 `on_init` 之前接收并暂存启动配置的入口——`configure` / `reconfigure`
的 `NotReady` 语义决定了单纯给钩子还不够。两处都落地之后，本层才接线并把失败诊断接进
`LogConfigError`。在此之前，第 3 节的默认值就是产品的全部日志行为。

## 5. 文件 sink 的边界（未实现 / 未验证，不得当作已支持）

| 项 | 状态 |
|---|---|
| 路径来源 | **未实现**：宿主没有读配置/环境变量的代码，没有默认日志路径 |
| 路径容量 | `kMaxFilePathBytes = 260`（含末尾 NUL）是 ABI 上限；插件会把末字节强制写 `'\0'` |
| Windows 窄字节路径 / UTF-8 / 宽文件系统 | **未实现、未取证**：`file_path` 是窄字节定容数组，非 ASCII 路径、控制台/系统代码页与 UTF-8 的关系、`\\?\` 超长前缀都没有验证；**不承诺**超长路径可用 |
| 260 字节边界 | **未取证**：恰好 259 字节、260 字节与超长输入的行为没有实测证据 |
| 轮转 | **未启用**：产品层不设置 `max_file_bytes` / `max_files`；轮转策略未定 |
| 多进程写同一文件 | **不支持，且未实现拒绝**：既没有拒绝逻辑，也没有 PID 隔离或按进程命名；多进程同时写同一路径不在契约内 |

## 6. 待测项（交 `Benchmark`，本层不加测试代码）

1. 启动期配置注入落地后的"配置非法"场景：启动期失败可诊断（不要求 `Fatal`），记录输出与退出码。
2. 配置拒绝与回滚档案（I02 余项）：`InvalidEnvelope`（`struct_size` / `config_revision` 不匹配）、
   `InvalidLevel`（`min_level` 越界）、`InvalidQueueSize`（异步队列非 2 的幂）、`MissingFilePath`、
   `FileNotWritable`、`NoSink`、`TopologyRequiresReconfigure`（拓扑字段走 `configure`）各返回失败且
   **状态不变**；`reconfigure` 失败时旧后端与旧状态完全不变。产品层目前没有触发这些路径的入口（见第 4 节）。
3. `file_path` 的 UTF-8 / 非 ASCII / 系统代码页边界；259 / 260 字节边界；`\\?\` 长路径前缀
   （超长路径若不可用，必须按"不支持"记录，不得写成可用）。
4. 多进程写同一日志文件：确认当前行为的可观测后果（当前无拒绝、无隔离）。
5. 轮转参数（`max_file_bytes` + `max_files`）双非 0 时的轮转与保留文件数。
6. `start` / `shutdown` 的重复调用与失败回滚路径（`code` / `state` / `slot` 诊断）。
7. 四配置（MinGW/MSVC × Debug/Release）与 SHARED/STATIC 的安装消费矩阵，含 `ship` profile 的
   `find_package(IDGameEngine)` + `ID::Engine::start()/shutdown()` 消费样例。

## 7. 兼容性与验证范围

已验证的平台与模式、以及未验证项只以真实构建/运行记录为准：本层不做性能评级、不写性能数字，
也不把静态推断写成已通过。证据位置是构建目录下的 `IDBuildInfo.txt`、
`Assemble/generated/<assembly_name>/IDAssemblyEvidence.txt`、`flags.make` 的 `CXX_DEFINES`
与 `Sandbox.exe` 的退出码与输出。

已取证的组合：MinGW GCC 16.2.0 x64、`Release`、SHARED、profile `ship`（选中 `ID.Log` + `ID.Math.Scalar`），
配置 / 构建 / 运行三步的**外部进程退出码**均为 `0`，stdout 为 `product started (state=Ready)` 与
`product stopped (state=Idle)`。这三条结论对应的可核产物：

| 步骤 | 退出码 | 落盘证据（相对工程根 `F:\IDGameEngine`） |
|---|---|---|
| configure | `0` | `Benchmark\build\ship-profile-l02-r3-configure.log` |
| build（`--target Sandbox`） | `0` | `Benchmark\build\ship-profile-l02-r3-build.log` |
| run | `0` | `Benchmark\build\ship-profile-l02-r3-run.stdout.txt`、`Benchmark\build\ship-profile-l02-r3-run.stderr.txt` |

汇总（命令原文、cwd、退出码、关键输出、产物时间戳一次性写在一起）：
`Benchmark\build\ship-profile-l02-r3-verify.txt`。配套元数据在同一构建目录
`Benchmark\build\ship-profile-l02\`：`IDBuildInfo.txt`（assembly / profile / mode / 编译器）、
`Assemble\generated\ship\IDAssemblyEvidence.txt`（产品清单）、
`Sandbox\CMakeFiles\Sandbox.dir\flags.make` 的 `CXX_DEFINES`
（`-DID_USE_PLUGIN_IDLOG=1 -DID_USE_PLUGIN_IDMATHSCALAR=1`，即编译期身份选择）。
重建同一组合的命令见第 1 节；换构建目录后必须重新落盘证据，不能沿用本表路径。

未取证（不得写成已支持）：MSVC 与 Debug 配置、`ID_MONOLITHIC_BUILD=ON`（STATIC）归并、
安装后 `find_package(IDGameEngine)` 的消费、以及第 4/5/6 节列出的启动期配置与文件 sink 相关能力。
