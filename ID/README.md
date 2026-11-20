# ID：项目创建、组装与构建工具

`ID` 是 IDGameEngine 的离线命令行工具。它不进入产品运行时，只在你敲命令的时候工作：读 JSON 配置，生成项目工程文件，把插件资产复制进项目，调用 CMake 构建出原生可执行文件。

## 1. 这是什么

两族命令：

| 命令族 | 作用 |
| --- | --- |
| `ID cmake self [--root <工程根>] [--check]` | 仓库自描述：重写 `IDPlugin` 下的四份生成构建描述。与用户项目无关。 |
| `ID project <子命令>` | 用户项目全流程：`new` / `update` / `build` / `clean` / `recover` / `list`。 |

一句话概括它做什么：

- **读**：引擎根的 `ID\project.json`（本机项目登记）、项目根的 `plugins.json`（你的插件选择）、`IDPlugin\<插件名>\plugin_config.json`（插件作者维护的清单）。
- **写**：项目根内由工具拥有的部分——根 `CMakeLists.txt` 的两个受控片段、`IDPlugin\**`（插件资产副本与聚合构建描述）、`IDGenerated\**`（记录、日志、备份素材）；以及本机登记的条目。
- **不做**：不做运行时；不自己写 Makefile / Ninja / Visual Studio 工程（一律交给 CMake）；不改你的 `src\**`、不改 `plugins.json` 的语义、不替你决定选哪个插件、不静默降级。

构建产物是 Windows 的 `.exe`（其它平台是原生可执行文件，不是 `.exe`）。

## 2. 前提

**可执行文件**：`<工程根>\bin\ID.exe`，本机即 `F:\IDGameEngine\bin\ID.exe`。

**CMake**：`cmake` 必须在 `PATH` 里，或用 `--cmake <绝对路径>` 指定。找不到时构建报 `build_invoke_failed`(4)。

**引擎根怎么来的**（工具按顺序找，取第一个命中的）：

1. 环境变量 `ID_ENGINE_ROOT`；
2. `ID.exe` 所在目录若名为 `bin`，取它的父目录；
3. 当前工作目录。

候选目录必须含 `ID\`；装配（`new` / `update`）还需要可读的 `IDPlugin\`。只含 `ID\` 且已有本机登记（`ID\project.json`）的目录也能被认作引擎根，`build` / `clean` / `recover` / `list` 因为不读源插件库，在这种引擎根下仍可用。全部候选都不成立时报 `project_path_invalid`(1)，诊断的「实际」会列出试过的每个来源。

**两条硬规则**：

- `--path` 必须是**绝对路径**。相对路径直接报 `project_path_invalid`(1)。
- 项目根不得落在引擎根内（也不得包含引擎根、`ID\`、`IDPlugin\`、`Benchmark\` 子树），不得是卷根、用户主目录或系统目录。违反报 `project_path_overlap`(1) 或 `project_path_invalid`(1)。

**项目名**：首字符必须是字母，其余是字母、数字、下划线或连字符。但连字符 `-` 会在生成 CMake 描述时被拒（`cmake_name_conflict`，1），所以实际请只使用 ASCII 字母、数字与下划线，例如 `hello`、`MyApp2`。

## 3. 五分钟上手

下面每一步都可以直接粘贴进 PowerShell 5.1。示例用临时目录；若你的 `%TEMP%` 含非 ASCII 字符（中文用户名常见），把 `$P` 换成纯 ASCII 路径，例如 `$P = 'C:\id-demo\hello'`。

```powershell
# 0) 变量：exe 与项目根（都要绝对路径）
$ID = 'F:\IDGameEngine\bin\ID.exe'
$P  = Join-Path $env:TEMP 'id-demo\hello'
New-Item -ItemType Directory -Force -Path $P | Out-Null
```

```powershell
# 1) 创建空项目骨架：不选任何插件
& $ID project new --name hello --path $P --empty
$LASTEXITCODE        # 期望 0
```

你会看到（形状）：

- 逐行 `<written|unchanged|removed|backup> <相对路径>`，例如 `written CMakeLists.txt`、`written src/main.cpp`；
- 收尾三行：`记录: IDGenerated/resolved.json`、`记录: IDGenerated/manifest.json`、`组装: 已提交`。

```powershell
# 2) 构建 debug
& $ID project build hello --generator "MinGW Makefiles" --toolchain mingw --arch x64 --config debug
$LASTEXITCODE        # 期望 0
```

你会看到（形状）：`产物: <项目根>\bin\mingw\x64\debug\hello.exe`、`构建结论: success`，以及两行 `记录:` 与一行 `组装:`。

```powershell
# 3) 运行产物
& (Join-Path $P 'bin\mingw\x64\debug\hello.exe')    # 打印 hello
```

生成并构建后的目录（`new --empty` 的项目）：

```text
<项目根>\
├── plugins.json            你的选择（空项目里 plugins 为空数组）
├── CMakeLists.txt          工具维护两个受控片段；片段外内容归你
├── src\main.cpp            首次创建；之后归你
├── IDGenerated\            工具生成区：不要手改
│   ├── resolved.json       本次组装的解析记录
│   ├── manifest.json       受管条目的归属与摘要
│   ├── build.json          构建记录（数组，每次构建追加一条）
│   ├── intent.json         只在一次未完成的操作期间存在
│   ├── .lock               只在命令运行期间存在
│   ├── before\  backup\    恢复与 --force 覆盖用的旧字节（按需出现）
│   └── log\<命令>-<配置>-<UTC>Z-<四位序号>.log
├── IDPlugin\               插件资产副本区
│   └── CMakeLists.txt      聚合构建描述（选中插件时另有 Common\ 与各插件目录）
├── build\<toolchain>\<arch>\<config>\   CMake 构建树
└── bin\<toolchain>\<arch>\<config>\<项目名>.exe
```

`<config>` 是 `debug` 或 `release`；`<arch>` 缺省为 `host`，所以想要确定目录就显式给 `--arch`。

### 已实测样例：IDJson 静态项目（MinGW x64 Debug）

本机实测（Windows + PowerShell 5.1，GCC 16.2.0 / CMake 4.4.4）用了这两条命令。下面各条的本机实测覆盖 `build` 与运行期观察；`new` 的退出码**未验证**（原因见下）。

```powershell
$ID = 'F:\IDGameEngine\bin\ID.exe'

& $ID project new   --name Knowbe --path 'F:\Knowbe' --plugin IDJson
& $ID project build Knowbe --generator "MinGW Makefiles" --toolchain mingw --arch x64 --config debug
```

- `build` 实测退出码 `0`，收尾打印 `产物:`、`构建结论: success`、两行 `记录:` 与 `组装: 已提交`；它写出一份 `IDGenerated\log\build-debug-<UTC>-<序号>.log`，并在 `IDGenerated\build.json` 追加一条：`generator`、`requested_toolchain` / `actual_toolchain`、`identity`（`GNU` / `16.2.0` / `x86_64-w64-mingw32` / 指针宽度 `64`）、`configure_exit` / `build_exit`、`verdict` 与 `artifacts` 的路径和摘要。
- `new` 是创建该项目的命令，但其退出码**未验证**：项目已存在时 `new` 不可重放，原因见下文「`new --check` 不可重放」。
- 生成结果：`plugins.json` 只含直接选择的 `IDJson`（`static`，`ENABLE_LOG=ON`）；被依赖补齐的 `IDLog` 只出现在 `IDGenerated\resolved.json`，取值为 `"linkage": "dynamic"`、`"is_direct": false`、`"linkage_source": "requirement"`。
- 产物：`<项目根>\bin\mingw\x64\debug\Knowbe.exe`（实测 57683 字节）；同一目录还有 `libIDJson.a`、`libIDLog.dll`、`libIDLog.dll.a`。
- 运行期：直接运行该 exe，实测退出码 `0`，stdout 是 `Knowbe`。`objdump -p` 显示它的导入表只有 `KERNEL32.dll` 与 8 个 UCRT 的 `api-ms-win-crt-*`，**没有** `libIDLog.dll`：样例 `src\main.cpp` 只调用 `std::puts`，没有引用 IDJson / IDLog 的任何符号，所以链接行虽然含 `libIDLog.dll.a`（`ENABLE_LOG=ON` 带进来的依赖），却没有符号被拉取——静态库成员与 IDLog 的导入项都不进产物。`libIDLog.dll` 由构建直接输出到 exe 同目录（见第 11 节），同目录是「被加载器找到」的前提，但装载路径本次**未验证**：要验证它，需要一个真正引用 IDLog 符号的 exe。
- 一致性检查可重放：`& $ID project update Knowbe --check` 实测退出码 `0`（写入 0 / 删除 0 / 冲突 0）。`new --check` 不可重放：项目已有 `plugins.json` 时再给 `--plugin` / `--empty` 的选择来源冲突规则见第 4 节末尾那条（`new` 时若项目根已经存在 `plugins.json`…）；本机只读实测的是 `--empty` 这一情形（`selection_source_conflict`，退出码 `3`）。
- 限制 `g8_engine_option_not_projected` 对本样例的含义：生成的插件目标不投影任何工程级选项与宏（清单十项字段没有表达「插件消费工程级选项」的位置），所以「项目生成的目标与工程内构建在 debug 行为上等价」不成立；该限制不改变退出码，也不代表本次生成结果不可用。

## 4. 选插件

`--plugin` 可以重复，一次只描述一个插件：

```text
--plugin <Name>[:static|dynamic][:OPTION=ON,OPTION2=OFF]
```

```powershell
$ID = 'F:\IDGameEngine\bin\ID.exe'

# 示例目录（目标目录也可以预先不存在，new 会按需创建；这里显式建好保证可复制）
foreach ($n in 'demo1','demo2','demo3','demo4','bad1','bad2') {
    New-Item -ItemType Directory -Force -Path "C:\id-demo\$n" | Out-Null
}

# 只选一个插件，链接形态按插件清单的缺省
& $ID project new --name demo1 --path C:\id-demo\demo1 --plugin IDJson

# 显式链接形态
& $ID project new --name demo2 --path C:\id-demo\demo2 --plugin "IDWindow:dynamic"

# 带选项（IDJson 声明了 ENABLE_LOG）
& $ID project new --name demo3 --path C:\id-demo\demo3 --plugin "IDJson:static:ENABLE_LOG=OFF"
```

规则：

- `--empty` 与 `--plugin` **互斥**；同时给出是用法错误 `cli_usage`(3)。
- 同一插件名一次命令里给出两次、或同一参数重复给出且取值不同，都是 `cli_usage`(3)：不合并、不去重、不后者覆盖前者。
- **依赖自动补齐**：只补具体模块依赖，补进来的插件写进 `IDGenerated\resolved.json`，**不改写你的 `plugins.json`**。例如选了 `IDJson`，它依赖的 `IDLog` 会被自动补齐。
- 链接形态有作者约束，写错报 `requirement_conflict`(1)，工具不会替你换形态：

| 插件 | 清单里的 `requirements` | 你能选的形态 |
| --- | --- | --- |
| `IDLog` | `SHARED_ONLY` | 只能 `dynamic` |
| `IDJson` | `STATIC_ONLY` | 只能 `static` |
| `IDWindow` | 空（两种皆可，缺省 `static`） | `static` 或 `dynamic` |

```powershell
# 这两种写法会被拒，退出码 1，类别 requirement_conflict
& $ID project new --name bad1 --path C:\id-demo\bad1 --plugin "IDLog:static"
& $ID project new --name bad2 --path C:\id-demo\bad2 --plugin "IDJson:dynamic"
```

`new` 时若项目根已经存在 `plugins.json`，再给 `--plugin` / `--empty` 会报 `selection_source_conflict`(3)：文件一旦存在就是唯一的选择来源，改选择请编辑该文件后跑 `update`。

不给任何选择来源时：在终端里会进入交互式选择；非交互环境（重定向、管道、CI）直接报 `cli_usage`(3) 并给出两种可复制的写法。

## 5. 日常闭环

```powershell
$ID = 'F:\IDGameEngine\bin\ID.exe'

# 1) 编辑 <项目根>\plugins.json（增删插件、改 linkage_type 或选项）
# 2) 按新选择重新组装
& $ID project update hello
$LASTEXITCODE        # 期望 0

# 3) 构建（debug 与 release 各自独立目录，互不覆盖）
& $ID project build hello --generator "MinGW Makefiles" --toolchain mingw --arch x64 --config debug
& $ID project build hello --generator "MinGW Makefiles" --toolchain mingw --arch x64 --config release
```

`plugins.json` 的形状（首次由 `new` 写出，之后由你维护）：

```json
{
    "name": "hello",
    "plugins": [
        {
            "name": "IDJson",
            "linkage_type": "static",
            "options": { "ENABLE_LOG": "ON" }
        }
    ]
}
```

- 必填键：顶层 `name`、`plugins`；每项 `name`、`linkage_type`、`options`（可以是空对象，但不能缺键）。未知字段会被拒绝（`project_config_invalid`，1）。
- 只写你**直接选择**的插件；依赖闭包不进这个文件。
- 顺序无语义。

**一步生成 + 构建**：`new` / `update` 加 `--build`：

```powershell
& $ID project new --name demo4 --path C:\id-demo\demo4 --plugin IDJson --build `
    --generator "MinGW Makefiles" --toolchain mingw --arch x64 --config debug
```

`--build` 时 `--config` 可省（缺省 `debug`），但首次构建仍需 `--generator`。

**换 `--arch` / `--config` 只换目录与 configure 变量**：它们决定 `build\<toolchain>\<arch>\<config>\` 与 `bin\<toolchain>\<arch>\<config>\`，以及传给 CMake 的 `-DCMAKE_BUILD_TYPE`、`-DID_BUILD_TOOLCHAIN`、`-DID_BUILD_ARCH`；**不改变组装身份**，不需要重跑 `update`。同一构建目录里已有 `CMakeCache.txt` 时，后续构建可以省略 `--generator`（但要给 `--toolchain`，否则定位不到构建目录）。

## 6. 先看再动

`--check` 与 `--dry-run` 是同一个开关：只读，输出「将要发生什么」，不写任何文件——不建锁、不建目录、不写日志、不改记录、不建构建目录。

```powershell
$ID = 'F:\IDGameEngine\bin\ID.exe'
$P  = Join-Path $env:TEMP 'id-demo\hello'      # 与第 3 节同一个项目根

& $ID project new  --name hello --path $P --empty --check
& $ID project update hello --check
& $ID project build  hello --config debug --generator "MinGW Makefiles" --toolchain mingw --arch x64 --check
& $ID project clean  hello --check
& $ID project recover hello --action continue --check
```

`new` / `update` / `clean` 的 `--check` 输出三类清单（顺序固定：写入 → 删除 → 冲突），形状如下（本机只读实测）：

```text
写入清单（<n> 项）:
    将写入 CMakeLists.txt
    将写入 IDPlugin/CMakeLists.txt
    将写入 src/main.cpp
删除清单（<n> 项）:
冲突清单（<n> 项）:
汇总: 写入 <n> / 删除 <n> / 冲突 <n>
退出码: 存在计划变更，退出码 6（不是失败，也不是已一致）
    将在登记文件里新增本项目记录（is_initialized = false）
    将写出首次 plugins.json
    将重写 is_initialized 派生缓存
    <限制码>: <说明>
```

- 写入项是 `将写入 <路径>`（受控片段写成 `将写入 CMakeLists.txt#<片段名>`），删除项是 `将删除 <路径>`，冲突项是 `写入冲突 <类别> <身份>：<原因>` 或 `裁剪冲突 <类别> <身份>：<原因>`。
- 最后几行是控制面动作（登记、`plugins.json`、派生标记）与 `limitations`；`limitations` 是限制说明，不是警告，不改变退出码。
- `build --check` 输出的是将执行的命令，形状为：`将执行一次构建（--check，不执行）:`，随后 `configure: <参数>`、`build: <参数>`、`构建目录: <路径>`。
- `recover --check` 输出逐项动作、`收敛动作: continue|restore`、`组装: ...`。

**退出码怎么读**：

| 码 | 含义 |
| --- | --- |
| `0` | 无计划变更，当前状态已经就是目标状态 |
| `6` | 有计划变更（会写入或删除），但**没有冲突**——不是失败，也不是「已一致」 |
| `2` | 计划里存在写入冲突（`unmanaged_content` 等） |
| `5` | 计划里存在裁剪冲突（`cleanup_blocked`） |
| `1` | 前置校验就没过（路径、选择、记录等），此时没有清单可看 |

## 7. 清理与恢复

**`clean`** 是离线命令：**不读** `plugins.json`、**不读**源插件库、**不重算**保留集（保留集只取自已提交的 `resolved.json` 字段）。因此源插件目录不可用、或你已经改过选择时，它照样能跑。

删除范围只有 `IDPlugin\**` 内、由已提交 `manifest.json` 证明归属、且不在保留集内的条目。正常已提交的项目里没有这类条目，所以 `clean` 常常是 no-op：不写 `intent`、不改记录、不动磁盘，退出码 `0`。它**不删除** `IDGenerated\**`、`build\**`、`bin\**`、`before\**`、`backup\**`，也不改 `generation_id`、不改登记的 `is_initialized`（保持 `true`），所以 `clean` 之后 `build` 仍然可用。`clean` 不代替 `update`：选择变了还是要 `update`。

```powershell
$ID = 'F:\IDGameEngine\bin\ID.exe'

& $ID project clean hello --check      # 先看：列出待删路径，零写盘
& $ID project clean hello              # 真删：逐条比对摘要后删除
```

**`clean --force` 的边界**：`--force` 只豁免「摘要一致」这一条——已登记但被本地改过的**待删**条目，给了 `--force` 会先备份到 `IDGenerated\backup\<generation>\` 再删。它**不放行**：未登记路径（`IDPlugin\**` 内既不在 `manifest.json`、也不在保留集里的文件）、越出项目根的路径。这两种情况一律 `cleanup_blocked`(5)，且是整阶段阻断——零删除、零提交，那些路径只能由你处置（移出 `IDPlugin\**`，或手工删除后重跑）。

**`recover`** 用于收敛一次未完成的操作：

```powershell
& $ID project recover hello --action continue --check    # 只看会做什么
& $ID project recover hello --action continue            # 续做：仅当内容已全部落盘、只剩记录收尾
& $ID project recover hello --action restore             # 撤销：按本次已校验的旧字节回滚工具拥有的路径
```

- `continue` 的适用窗口很窄：内容已经全部写对，只剩记录提交与收尾。内容还有没落盘的项时报 `recovery_conflict`(2)，诊断会给出两条可复制的命令。
- `restore` 用本次记录在案的旧字节，撤销这次操作对工具拥有路径与受控片段的改动；**不回滚你自己改过的文件**。
- 项目里没有未完成的 `intent` 时，`recover` 报 `recovery_conflict`(2)，不当作「无事可做的成功」。
- **普通失败优先重跑原命令**。例如 `update` 中途失败，直接再跑一次 `ID project update hello` 就是正确做法（会按当前磁盘状态续做）；只有想撤销时才用 `recover --action restore`。

## 8. 看状态

```powershell
& 'F:\IDGameEngine\bin\ID.exe' project list
```

每行一个已登记项目，按**项目名的字节序**排列，列之间用两个空格分隔：

```text
<项目名>  <路径>  <initialized|pending>  [missing]  [duplicate]  [record: ok|invalid]
```

| 列 | 含义 |
| --- | --- |
| 项目名 / 路径 | 来自本机登记 `ID\project.json` |
| `initialized` / `pending` | `initialized` = 该项目有已提交的组装；`pending` = 还没有。这是派生缓存，真源是项目里的两份记录 |
| `missing` | 该路径当前不存在或不是目录 |
| `duplicate` | 该行与另一行同名或同路径（重复登记不是致命错误，但 `update` / `build` / `clean` 会报 `registry_invalid`） |
| `record: ok` / `record: invalid` | 项目里两份记录都存在且互相自洽 / 存在但不自洽；两份都不存在时这一列不出现 |

登记文件本身读不了或结构非法时报 `registry_invalid`(1) 并终止。

## 9. 诊断怎么读

失败时 stderr 打印诊断，每条固定六行（五要素 + 定位）：

```text
    类别: <snake_case 标识>（<中文说明>）
    对象: <项目名 / 插件名 / 条目身份 / 路径>
    期望: <工具要求的状态>
    实际: <实际发生了什么>
    定位: <相对路径或阶段；没有时写 （无）>
    下一步: <可复制的动作>
```

成功时不打印诊断。脚本判断成败只看退出码，不要解析日志文本。

常见类别：

| `类别` | 含义 | 怎么做 |
| --- | --- | --- |
| `cli_usage` (3) | 参数不合法：未知参数、缺取值、互斥组合、`--check` 与 `--build` 同给、同一插件给两次 | 按「下一步」给出的完整命令重跑 |
| `selection_source_conflict` (3) | `plugins.json` 已存在，却又给了 `--plugin` / `--empty` | 去掉选择参数，改编辑 `plugins.json` 后 `update` |
| `project_exists` (1) | 项目名或路径已被占用；或 `new` 指向非空且未登记的目录 | 换名字或换空目录；已登记项目直接 `update`。`--force` 不放行 |
| `project_path_invalid` (1) | 路径不是绝对路径、不存在、不可读；或是卷根/主目录/系统目录；或引擎根找不到 | 改成已存在的绝对普通目录；引擎根问题按「实际」里列出的来源逐个排除 |
| `project_path_overlap` (1) | 项目根与引擎根（或 `ID` / `IDPlugin` / `Benchmark` 子树）互相包含 | 把项目挪到独立的绝对路径 |
| `project_config_invalid` (1) | `plugins.json` 解析失败、缺必填键、未知字段、`linkage_type` 取值非法 | 对照第 5 节的形状修文件 |
| `project_config_missing` (1) | `update` / `build` 找不到 `plugins.json` | 恢复该文件；它是唯一的选择来源 |
| `plugin_unknown` (1) | 选择或依赖引用了 `IDPlugin` 下没有的插件（或该插件清单非法已被忽略，会附原始原因） | 检查插件名与 `IDPlugin\<名>\plugin_config.json` |
| `requirement_conflict` (1) | 你给的 `linkage_type` 与插件清单的 `requirements` 冲突 | 改 `plugins.json` 的 `linkage_type`，或让插件作者调整 `requirements` |
| `option_unknown` / `option_value_invalid` (1) | 选项没在清单里声明 / 取值不是 `ON` 或 `OFF`（大小写敏感） | 按插件清单的 `options` 修正 |
| `stale_generated` (1) | 已提交的组装与当前选择不一致（典型：改了 `plugins.json` 却没 `update` 就 `build`）；或重复 `new` 同路径但选择/受管树已变 | 跑 `update` |
| `unmanaged_content` (2) | 要覆盖的目标被本地改过，或路径未登记 | 要保留就移出生成区；要放弃就 `update --force`。未登记路径任何 `--force` 都不接管 |
| `cleanup_blocked` (5) | 待删范围内有未登记路径或被本地改过的条目 | 未登记路径只能你自己处置；被改过的条目可 `clean --force`（先备份再删） |
| `record_invalid` (2) | 两份记录缺失、损坏或互不一致 | 运行 `update` 从可信来源重建；不要手改 `IDGenerated\**` |
| `recovery_conflict` (2) | 有未完成的 `intent` 与当前命令/状态冲突 | 重跑原命令，或 `recover --action continue\|restore` |
| `path_outside_project` (1) | 条目路径越出项目根或形态非法（绝对路径、盘符、`..`、保留名等） | 记录被外部改坏；重跑 `update` 重建。`--force` 不放行 |
| `write_failed` (2) | 写入、替换或记录落盘失败（占用、权限、磁盘） | 排除占用/权限后重跑 |
| `build_environment_unsupported` (4) | 生成器不受支持、同一构建目录已有别的生成器记录、`--config` 取值非法、实测构建身份与请求不一致 | 按「下一步」改用单配置生成器或换目录段 |
| `build_invoke_failed` (4) | `cmake` 进程起不来（找不到或不可执行） | 把 `cmake` 加进 `PATH`，或用 `--cmake <绝对路径>` |
| `build_failed` (4) | configure 或 build 返回非零 | 按 `IDGenerated\log\*.log` 里的报错修源码；组装结果不变 |
| `artifact_missing` (4) | 构建返回 0 但期望的可执行文件不成立 | 检查根 `CMakeLists.txt` 的目标名与输出目录是否被改过 |

另有三个 `warn` 类别不改变退出码：`registry_marker_stale`（登记的初始化标记待收尾，跑无内容变化的 `update` 即可修）、`plugin_config_invalid`、`dependency_unknown`（后两者表示某个插件清单非法/依赖未知，该插件被忽略；只要没人引用它就不影响结果）。

## 10. `ID cmake self`

保留的仓库自描述命令，与用户项目无关：

```powershell
$Root = 'F:\IDGameEngine'

& "$Root\bin\ID.exe" cmake self --root $Root            # 生成
& "$Root\bin\ID.exe" cmake self --root $Root --check    # 只比较
```

- 产出 `IDPlugin\CMakeLists.txt`、`IDPlugin\IDLog\CMakeLists.txt`、`IDPlugin\IDJson\CMakeLists.txt`、`IDPlugin\IDWindow\CMakeLists.txt` 四份文件。
- `--root` 缺省为当前工作目录。
- 每个目标文件一行：`written` / `unchanged` / `failed:<类别> <相对路径>`。
- 退出码：`0` 成功（`--check` 表示四份全部一致）、`1` 校验失败或 `--check` 检出不一致、`2` 写入失败、`3` 用法错误。

**不要和 `project` 的 `--check` 混用**：`cmake self --check` 的 `1` 是「文件与模型产出的内容不一致」；`project --check` 的 `6` 是「有计划变更」。两者语义不同，脚本里要分别判断。

本机只读实测输出形状：

```text
unchanged IDPlugin/CMakeLists.txt
unchanged IDPlugin/IDJson/CMakeLists.txt
unchanged IDPlugin/IDLog/CMakeLists.txt
unchanged IDPlugin/IDWindow/CMakeLists.txt
```

## 11. 边界与限制

以下是**当前不支持或未验证**的内容，不要当成已支持：

- **只支持单配置生成器**：`MinGW Makefiles`、`Unix Makefiles`、已在 `vcvarsall` 环境里的 `Ninja`。`Visual Studio`、`Xcode`、`Ninja Multi-Config` 是多配置生成器，工具在写盘与 configure 之前就报 `build_environment_unsupported`(4)。
- **配置只有 `Debug` / `Release`**：`--config` 只接受小写 `debug` 或 `release`，其它取值报 `build_environment_unsupported`(4)。`build` 必须显式给 `--config`。
- **首次构建必须显式给 `--generator`**：工具不从环境猜生成器；只有同一构建目录里已有 `CMakeCache.txt` 时，后续构建才可以省略 `--generator`（此时要给 `--toolchain` 以定位目录）。`Ninja` 没有内建工具链标签，必须同时给 `--toolchain`。
- **纯生成不需要 profile**：`new` / `update` 不带 `--build` 时不调用编译器，不需要 `--generator` / `--toolchain` / `--arch` / `--config`。
- **项目路径含非 ASCII 字符时，本机 `MinGW Makefiles` 构建会失败**：原生 `mingw32-make` 会按 ANSI 代码页转交路径参数，子进程拿到乱码路径。工具会在诊断的「下一步」里指出这一点并建议改用 `Ninja` 或把项目移到纯 ASCII 路径。路径含**空格**没有问题。
- **共享插件 DLL 靠「与 exe 同目录」被找到，不靠 RPATH**：Windows 没有 RPATH 机制，`BUILD_RPATH "$ORIGIN"` 在 MinGW 下也不产生 `-Wl,-rpath`（本机实测 exe 的链接行没有该参数）。工具把共享插件库直接输出到 `bin\<toolchain>\<arch>\<config>\`，与 exe 同目录；把产物搬到别处时要自己带上 DLL，否则引用到该插件符号的 exe 会在启动时装不上它。本条中「不产生 `-Wl,-rpath`」是本机实测，「DLL 与 exe 同目录即被加载器找到」属 Windows 加载器语义与设计约定，**尚未**用真正引用共享插件符号的 exe 实测——本机样例的 exe 导入表里没有 `libIDLog.dll`（见第 3 节）。
- **生成的项目不能直接丢给 IDE 配置**：根 `CMakeLists.txt` 的 `output-dirs` 受控片段要求 configure 时传入 `-DID_BUILD_TOOLCHAIN` 与 `-DID_BUILD_ARCH`，缺了直接 `FATAL_ERROR`（`ID_OUTPUT_ROOT 缺少身份段`，实测 cmake 退出码 `1`）。要让 IDE 用，就在它的构建配置里补这两个变量；否则一律走 `ID project build`。
- **Linux 与 MSVC 组合未实测**：代码里有对应分支，但本机没有验证过，不承诺可用；需要时自行验证并把结果反馈。
- **`artifact_missing` 场景未实测**：产物校验的判据已在代码里实现，但「构建成功而产物不在约定位置」这条路径本机没有实际触发过。
- **`IDGenerated\**` 是记录，不要手改**：`resolved.json`、`manifest.json`、`build.json`、`intent.json`、`log\`、`before\`、`backup\`。手工改动会被识别为 `unmanaged_content` / `record_invalid`；要恢复就重跑 `update`（必要时 `update --force`，它会先备份再覆盖）。
- **哪些归你**：`src\**`、`plugins.json`、根 `CMakeLists.txt` 中两个受控片段**之外**的内容、`build\**` / `bin\**`（CMake 产生的，可删可重建）。工具不修改这些，也不覆盖你说过要保留的改动。
- **`--force` 不是万能开关**：它只豁免「摘要一致」这一条，不放行未登记路径、不放行非空且未登记的目录、不放行越出项目根的路径。
- **不承诺崩溃后整项目原子**：多文件写入靠 `IDGenerated\intent.json` 逐项记录进度，崩溃/断电后用 `recover` 收敛，而不是「自动回滚到位」。

## 12. 退出码表

脚本只按退出码判断成败。

| 码 | 含义 |
| --- | --- |
| `0` | 成功；`--check` 无差异 |
| `1` | 输入或一致性校验失败（路径、名称、选择、清单、记录一致性、`stale_generated` 等） |
| `2` | 写入、记录、恢复或并发相关失败（`unmanaged_content`、`write_failed`、`record_invalid`、`recovery_conflict`、`changed_since_plan`、`snapshot_unstable`） |
| `3` | 用法错误（未知子命令/参数、缺取值、互斥组合、选择来源冲突） |
| `4` | 构建环境或构建失败（含 `cmake` 起不来、产物不成立） |
| `5` | 清理被阻塞（`cleanup_blocked`） |
| `6` | `--check` 检出计划差异——**不是失败**，也不是「已一致」 |

多个诊断同时成立时只取一个码，优先级为 `3 > 1 > 2 > 5 > 4 > 6 > 0`（这是优先级顺序，不是数值比较）。`warn` 类别不改变退出码。

## 13. 命令速查

```text
用法: ID project <子命令> [参数]
  new --name <项目名> --path <绝对路径> (--plugin <spec>... | --empty) [--check] [--build]
      [--force] [--config debug|release] [--generator <name>] [--toolchain <tag>] [--arch <tag>] [--cmake <path>]
  update <项目名> [--check] [--build] [--config ...] [--generator ...] [--toolchain ...] [--arch ...] [--cmake ...] [--force]
  build <项目名> --config debug|release [--generator ...] [--toolchain ...] [--arch ...] [--cmake ...] [--check]
  clean <项目名> [--check] [--force]
  recover <项目名> --action continue|restore [--check]
  list
```

| 子命令 | 完整参数形态 | 说明 |
| --- | --- | --- |
| `new` | `new --name <项目名> --path <绝对路径> (--plugin <spec>... \| --empty) [--check] [--build] [--force] [--config debug\|release] [--generator <name>] [--toolchain <tag>] [--arch <tag>] [--cmake <path>]` | 首次登记并生成；`--force` 被接受但不放行非空目录 |
| `update` | `update <项目名> [--check] [--build] [--config ...] [--generator ...] [--toolchain ...] [--arch ...] [--cmake ...] [--force]` | 按当前 `plugins.json` 重新组装 |
| `build` | `build <项目名> --config debug\|release [--generator ...] [--toolchain ...] [--arch ...] [--cmake ...] [--check]` | 只构建，不改组装；必须给 `--config` |
| `clean` | `clean <项目名> [--check] [--force]` | 离线裁剪到保留集 |
| `recover` | `recover <项目名> --action continue\|restore [--check]` | 收敛未完成的操作 |
| `list` | `list` | 列本机登记，不接受任何参数 |

参数含义：

- `--plugin <Name>[:static|dynamic][:OPTION=ON,OPTION2=OFF]`：可重复，一个参数只描述一个插件。
- `--empty`：显式声明不选任何插件。
- `--check` / `--dry-run`：只读，输出计划差异，不写任何文件；与 `--build` 互斥。
- `--build`：生成后继续构建（`new` / `update`）。
- `--force`：只对 `update` / `clean` 生效；`new` 接受该参数但不放行非空目录。
- `--config debug|release`、`--generator <name>`、`--toolchain <tag>`、`--arch <tag>`、`--cmake <path>`：构建 profile。
- `--action continue|restore`：`recover` 必填。

另外：`ID cmake self [--root <工程根>] [--check]`（见第 10 节）。无参数或缺子命令时打印用法并退出 `3`。
