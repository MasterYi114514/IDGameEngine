# =============================================================================
# 安装消费矩阵驱动器（Benchmark 校验资产；默认不参与产品构建，只能手工/独立驱动）
#
#   作用：对 Windows × {MinGW, MSVC} × {Debug, Release} × {SHARED, STATIC} 八格逐格执行
#         "产品 configure -> build -> install 到本格独占临时前缀 -> 独立消费工程 find_package
#          -> build -> 运行 -> 记录外部退出码"，另外补三格数学组合（Scalar only / SIMD only / both）。
#   约束：本脚本只写系统临时目录与 benchmark/validation/install-consumer 下的证据文件；
#         不改产品源码、不改 profile、不碰官方 report.txt、不做性能评级。
#   用法：powershell -NoProfile -ExecutionPolicy Bypass -File <本文件> [-Cell <名字...>] [-KeepWork]
# =============================================================================

[CmdletBinding()]
param(
    [string[]] $Cell   = @(),
    [switch]   $KeepWork
)

$ErrorActionPreference = 'Stop'

# ---- 工程路径：从工作台配置解析，不写死盘符 ---------------------------------
$WorkbenchRoot = 'f:\IDWorkbench'
$ConfigPath    = Join-Path $WorkbenchRoot 'workbench_config_local.json'
if (-not (Test-Path -LiteralPath $ConfigPath))
{
    throw "找不到工作台配置：$ConfigPath"
}
$WorkbenchConfig = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$EngineRoot      = Split-Path -Parent $WorkbenchConfig.Paths.IDPath
$ConsumerSrc     = Join-Path $EngineRoot 'Benchmark\validation\install-consumer'
$EvidenceFile    = Join-Path $ConsumerSrc 'install-consumer-evidence.txt'

# ---- 工具链：绝对路径来自工作台本机 skill，避免 PATH 漂移 -------------------
$CMake      = 'D:\Code\Environment\CMake\bin\cmake.exe'
$Gcc        = 'D:\Code\Environment\MinGW\mingw64\bin\g++.exe'
$MinGWBin   = 'D:\Code\Environment\MinGW\mingw64\bin'
$VsInstance = 'D:/Software/VS2026/Insiders'
$VsGenerator = 'Visual Studio 18 2026'
$Dumpbin     = 'D:\Software\VS2026\Insiders\VC\Tools\MSVC\14.52.37025\bin\Hostx64\x64\dumpbin.exe'

$WorkRootDefault = Join-Path $env:LOCALAPPDATA 'Temp\id-install-consumer'
$WorkRoot = $WorkRootDefault
if (-not (Test-Path -LiteralPath $WorkRoot))
{
    New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null
}

# ---- 证据累积 ---------------------------------------------------------------
$script:Evidence = New-Object System.Collections.Generic.List[string]
$script:Cells    = New-Object System.Collections.Generic.List[object]

function Add-Evidence
{
    param([string] $Text)
    $script:Evidence.Add($Text)
}

function Format-ExitCode
{
    param([int] $Code)
    # 0xFFFFFFFFL 是 int64 的 4294967295：PowerShell 5.1 里 0xFFFFFFFF 会退化成 -1，不能直接用
    $unsigned = [uint32]([int64]$Code -band 0xFFFFFFFFL)
    return ('{0} (0x{1:X8})' -f $Code, $unsigned)
}

# 运行一次原生命令并记录命令、工作目录、退出码、输出与耗时
function Invoke-Native
{
    param(
        [Parameter(Mandatory = $true)][string]   $Exe,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]] $Arguments,
        [Parameter(Mandatory = $true)][string]   $WorkingDirectory,
        [Parameter(Mandatory = $true)][string]   $LogFile
    )

    $display = "$Exe " + ($Arguments -join ' ')
    Write-Host "    > $display"

    $record = [pscustomobject]@{
        Command = $display
        Cwd     = $WorkingDirectory
        Exit    = -1
        Output  = @()
        Seconds = 0.0
    }

    $logDir = Split-Path -Parent $LogFile
    if (-not (Test-Path -LiteralPath $logDir))
    {
        New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    }

    # 用 Start-Process 把 stdout / stderr 分别重定向到文件：
    #   1. PowerShell 5.1 用 `&` + `2>&1` 时会把 stderr 变成 ErrorRecord，配合 Stop 策略会误判失败；
    #   2. 分开取流才能把产品的内核日志（走 stderr）与消费程序的检查结果（走 stdout）分别留证。
    # -ArgumentList 收到的是已手工加引号的单一字符串：Start-Process 不再替数组元素补引号，
    # 因此含空格的参数（"MinGW Makefiles"）必须在这里加引号。
    $quoted = @($Arguments | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
    }) -join ' '

    $stdoutFile = "$LogFile.stdout"
    $stderrFile = "$LogFile.stderr"

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    try
    {
        # Start-Process 不接受空的 -ArgumentList：无参进程要单独走一条不含该参数的调用
        if ([string]::IsNullOrEmpty($quoted))
        {
            $proc = Start-Process -FilePath $Exe -WorkingDirectory $WorkingDirectory `
                -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
        }
        else
        {
            $proc = Start-Process -FilePath $Exe -ArgumentList $quoted -WorkingDirectory $WorkingDirectory `
                -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
        }
        $record.Exit = $proc.ExitCode
    }
    catch
    {
        $record.Exit = -999
        [IO.File]::WriteAllText($stdoutFile, '', (New-Object System.Text.UTF8Encoding($false)))
        [IO.File]::WriteAllText($stderrFile, ('EXCEPTION: ' + $_.Exception.ToString()),
            (New-Object System.Text.UTF8Encoding($false)))
    }
    finally
    {
        $timer.Stop()
        $record.Seconds = [math]::Round($timer.Elapsed.TotalSeconds, 1)
    }

    $stdoutLines = @()
    $stderrLines = @()
    if (Test-Path -LiteralPath $stdoutFile) { $stdoutLines = @(Get-Content -LiteralPath $stdoutFile -Encoding UTF8) }
    if (Test-Path -LiteralPath $stderrFile) { $stderrLines = @(Get-Content -LiteralPath $stderrFile -Encoding UTF8) }
    $record.Output = @($stdoutLines) + @($stderrLines | ForEach-Object { 'stderr: ' + $_ })

    $logText = @($record.Command, "cwd: $($record.Cwd)", "exit: $(Format-ExitCode $record.Exit)",
        '--- stdout ---') + $stdoutLines + @('--- stderr ---') + $stderrLines
    [IO.File]::WriteAllText($LogFile, (($logText -join "`r`n") + "`r`n"),
        (New-Object System.Text.UTF8Encoding($false)))

    return $record
}

function Get-Tail
{
    param([string[]] $Lines, [int] $Count = 40)
    if ($null -eq $Lines -or $Lines.Count -eq 0)
    {
        return @('<empty>')
    }
    $start = [math]::Max(0, $Lines.Count - $Count)
    return $Lines[$start..($Lines.Count - 1)]
}

function Read-TextOrEmpty
{
    param([string] $Path)
    if (Test-Path -LiteralPath $Path)
    {
        return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8)
    }
    return ''
}

function Get-SelectLines
{
    param([string] $Path, [string] $Pattern)
    $text = Read-TextOrEmpty $Path
    if (-not $text)
    {
        return @()
    }
    return @($text -split "`r?`n" | Where-Object { $_ -match $Pattern })
}

function Get-InstalledListing
{
    param([string] $Prefix)
    $result = New-Object System.Collections.Generic.List[string]
    foreach ($sub in @('bin', 'lib'))
    {
        $dir = Join-Path $Prefix $sub
        if (Test-Path -LiteralPath $dir)
        {
            $names = @(Get-ChildItem -LiteralPath $dir -File | Sort-Object Name | ForEach-Object { $_.Name })
            if ($names.Count -eq 0)
            {
                $result.Add(("{0}/: <empty>" -f $sub))
            }
            else
            {
                $result.Add(("{0}/: {1}" -f $sub, ($names -join ', ')))
            }
        }
        else
        {
            $result.Add(("{0}/: <missing>" -f $sub))
        }
    }
    return $result.ToArray()
}

# VS 生成器的 vcxproj 把全部配置写在同一个文件里：取证必须按本格的 Configuration 过滤，
# 否则 Debug 的 /MTd 与 Release 的 /MT 会混在一起，看不出本格到底用了哪一个。
function Get-MsvcConfigGroupText
{
    param([string] $ConsumerBuild, [string] $Config)
    $projectFile = Join-Path $ConsumerBuild 'install_consumer.vcxproj'
    if (-not (Test-Path -LiteralPath $projectFile))
    {
        return ''
    }
    $text = [IO.File]::ReadAllText($projectFile)
    $pattern = "<ItemDefinitionGroup Condition=""[^""]*'" + [regex]::Escape($Config) + "\|x64'""[\s\S]*?</ItemDefinitionGroup>"
    $found = [regex]::Matches($text, $pattern)
    if ($found.Count -eq 0)
    {
        return ''
    }
    return $found[0].Value
}

function Get-MsvcRuntimeEvidence
{
    param([string] $ConsumerBuild, [string] $Config)
    $group = Get-MsvcConfigGroupText -ConsumerBuild $ConsumerBuild -Config $Config
    if (-not $group)
    {
        return @(("<vcxproj 中找不到配置组 {0}|x64>" -f $Config))
    }
    $matches = @([regex]::Matches($group, '<RuntimeLibrary>([^<]+)</RuntimeLibrary>') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($matches.Count -eq 0)
    {
        return @('<该配置组里没有 RuntimeLibrary 条目>')
    }
    return @($matches | ForEach-Object { "vcxproj[{0}] RuntimeLibrary={1}" -f $Config, $_ })
}

function Get-MsvcDefinitions
{
    param([string] $ConsumerBuild, [string] $Config)
    $group = Get-MsvcConfigGroupText -ConsumerBuild $ConsumerBuild -Config $Config
    if (-not $group)
    {
        return @(("<vcxproj 中找不到配置组 {0}|x64>" -f $Config))
    }
    $raw = @([regex]::Matches($group, '<PreprocessorDefinitions>([^<]+)</PreprocessorDefinitions>') |
        ForEach-Object { $_.Groups[1].Value })
    $sets = @($raw | ForEach-Object {
        $defs = @(($_ -split ';') | Where-Object { $_ -match '^ID_[A-Z0-9_]+(=.*)?$' } | Sort-Object)
        $defs -join ' '
    } | Sort-Object -Unique)
    return @($sets | ForEach-Object { "vcxproj[$( $Config )] ID_* defines: $_" })
}

function Get-MingwDefinitions
{
    param([string] $ConsumerBuild)
    $flagsFile = Join-Path $ConsumerBuild 'CMakeFiles\install_consumer.dir\flags.make'
    if (-not (Test-Path -LiteralPath $flagsFile))
    {
        return @('<flags.make 不存在>')
    }
    return @(Get-SelectLines -Path $flagsFile -Pattern '^CXX_DEFINES')
}

# MinGW 侧真正说明问题的证据是 exe 的动态依赖表：静态 CRT 生效时不应再出现
# libstdc++-6.dll / libgcc_s_seh-1.dll；libwinpthread-1.dll 仍动态（见 id-log skill 11.2）。
function Get-MingwDllImports
{
    param([string] $Executable)
    $objdump = Join-Path $MinGWBin 'objdump.exe'
    if (-not (Test-Path -LiteralPath $objdump))
    {
        return @('<objdump.exe 不存在>')
    }
    if (-not (Test-Path -LiteralPath $Executable))
    {
        return @('<consumer exe 不存在>')
    }
    $raw = @(& $objdump -p $Executable 2>&1 | ForEach-Object { $_.ToString() })
    $imports = @($raw | Where-Object { $_ -match 'DLL Name:' } | ForEach-Object { $_.Trim() })
    if ($imports.Count -eq 0)
    {
        return @('<objdump 未列出任何 DLL Name 条目>')
    }
    return $imports
}

function Get-MingwLinkEvidence
{
    param([string] $ConsumerBuild)
    $linkFile = Join-Path $ConsumerBuild 'CMakeFiles\install_consumer.dir\link.txt'
    if (-not (Test-Path -LiteralPath $linkFile))
    {
        return @('<link.txt 不存在>')
    }
    $text = Read-TextOrEmpty $linkFile
    $found = New-Object System.Collections.Generic.List[string]
    foreach ($token in @('-static-libstdc++', '-static-libgcc'))
    {
        if ($text -like "*$token*")
        {
            $found.Add("link.txt 含 $token")
        }
        else
        {
            $found.Add("link.txt 不含 $token")
        }
    }
    # libwinpthread 由工具链默认链接，不在 link.txt 里显式出现，也仍然动态：真实判据看下面的导入表
    $found.Add('libwinpthread 由工具链默认链接、仍动态（以导入表为准）')
    return $found.ToArray()
}

# 分配桩必须以 whole-archive 方式进最终程序（否则 new/delete 劫持符号可能不被提取）：
# 这条接口来自安装导出的 IDGameEngine INTERFACE，属于安装后可消费性的一部分。
function Get-WholeArchiveEvidence
{
    param([object] $Spec, [string] $ConsumerBuild)
    if ($Spec.Toolchain -eq 'mingw')
    {
        $rsp = Join-Path $ConsumerBuild 'CMakeFiles\install_consumer.dir\linkLibs.rsp'
        if (-not (Test-Path -LiteralPath $rsp))
        {
            return @('<linkLibs.rsp 不存在>')
        }
        $text = Read-TextOrEmpty $rsp
        return @(("linkLibs.rsp: whole-archive={0}  IDPluginStub 在库清单={1}" -f
            ($text -like '*--whole-archive*'), ($text -like '*libIDPluginStub.a*')))
    }
    $group = Get-MsvcConfigGroupText -ConsumerBuild $ConsumerBuild -Config $Spec.Config
    if (-not $group)
    {
        return @(("<vcxproj 中找不到配置组 {0}|x64>" -f $Spec.Config))
    }
    return @(("/WHOLEARCHIVE 出现在链接选项={0}" -f ($group -like '*WHOLEARCHIVE*')))
}

# MSVC 侧的真实判据同样是 exe 的依赖表：/MT 生效时不应出现 VCRUNTIME*/MSVCP*/ucrt 的 DLL。
function Get-MsvcDllImports
{
    param([string] $Executable)
    if (-not (Test-Path -LiteralPath $Dumpbin))
    {
        return @('<dumpbin.exe 不存在>')
    }
    if (-not (Test-Path -LiteralPath $Executable))
    {
        return @('<consumer exe 不存在>')
    }
    $raw = @(& $Dumpbin /nologo /dependents $Executable 2>&1 | ForEach-Object { $_.ToString() })
    $imports = @($raw | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
    if ($imports.Count -eq 0)
    {
        return @('<dumpbin 未列出任何依赖 DLL>')
    }
    return @($imports | ForEach-Object { 'DLL Name: ' + $_ })
}

# ---- 单元定义 ---------------------------------------------------------------
function New-CellSpec
{
    param(
        [string] $Name, [string] $Toolchain, [string] $Config, [string] $Mode, [string] $Profile,
        [bool] $ExpectIdLog, [bool] $ExpectScalar, [bool] $ExpectSimd, [string] $MathBackend
    )
    return [pscustomobject]@{
        Name         = $Name
        Toolchain    = $Toolchain
        Config       = $Config
        Mode         = $Mode
        Profile      = $Profile
        ExpectIdLog  = $ExpectIdLog
        ExpectScalar = $ExpectScalar
        ExpectSimd   = $ExpectSimd
        MathBackend  = $MathBackend
    }
}

$allCells = @(
    # ---- 8 格主矩阵：交付 profile（ship = ID.Log + ID.Math.Scalar，显式列出两个 provider） ----
    (New-CellSpec 'mingw-debug-shared'      'mingw' 'Debug'   'shared' 'ship' $true $true  $false ''),
    (New-CellSpec 'mingw-release-shared'    'mingw' 'Release' 'shared' 'ship' $true $true  $false ''),
    (New-CellSpec 'mingw-debug-static'      'mingw' 'Debug'   'static' 'ship' $true $true  $false ''),
    (New-CellSpec 'mingw-release-static'    'mingw' 'Release' 'static' 'ship' $true $true  $false ''),
    (New-CellSpec 'msvc-debug-shared'       'msvc'  'Debug'   'shared' 'ship' $true $true  $false ''),
    (New-CellSpec 'msvc-release-shared'     'msvc'  'Release' 'shared' 'ship' $true $true  $false ''),
    (New-CellSpec 'msvc-debug-static'       'msvc'  'Debug'   'static' 'ship' $true $true  $false ''),
    (New-CellSpec 'msvc-release-static'     'msvc'  'Release' 'static' 'ship' $true $true  $false ''),
    # ---- 三组合：MinGW Release SHARED 上各做一个 consumer ----
    (New-CellSpec 'mingw-release-shared-math-scalar' 'mingw' 'Release' 'shared' 'math-scalar' $false $true  $false ''),
    (New-CellSpec 'mingw-release-shared-math-simd'   'mingw' 'Release' 'shared' 'math-simd'   $false $false $true  ''),
    (New-CellSpec 'mingw-release-shared-math-both'   'mingw' 'Release' 'shared' 'math-both'   $false $true  $true  'simd')
)

$selectedCells = $allCells
if ($Cell.Count -gt 0)
{
    $selectedCells = @($allCells | Where-Object { $Cell -contains $_.Name })
    if ($selectedCells.Count -eq 0)
    {
        throw "没有匹配的格：$($Cell -join ', ')"
    }
}

# 期望值不手抄产品语义：直接从 Assemble/profiles/<名字>.json 的 enabled_plugins 派生，
# 这样"产品 profile 的选中集合"与"消费 TU 实际拿到的身份宏"才是两份独立来源的对照。
function Get-ProfileExpectations
{
    param([string] $ProfileName)
    $path = Join-Path $EngineRoot ("Assemble\profiles\{0}.json" -f $ProfileName)
    if (-not (Test-Path -LiteralPath $path))
    {
        throw "找不到 profile：$path"
    }
    $json  = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    $names = @($json.enabled_plugins)
    return [pscustomobject]@{
        Path   = $path
        Names  = $names
        IdLog  = ($names -contains 'ID.Log')
        Scalar = ($names -contains 'ID.Math.Scalar')
        Simd   = ($names -contains 'ID.Math.SIMD')
    }
}

function Get-GeneratorArguments
{
    param([object] $Spec)
    if ($Spec.Toolchain -eq 'mingw')
    {
        return @('-G', 'MinGW Makefiles', "-DCMAKE_CXX_COMPILER=$Gcc")
    }
    return @('-G', $VsGenerator, '-A', 'x64', "-DCMAKE_GENERATOR_INSTANCE=$VsInstance")
}

function Get-MonoFlag
{
    param([string] $Mode)
    if ($Mode -eq 'static') { return 'ON' }
    return 'OFF'
}

function Get-ConsumerExecutable
{
    param([object] $Spec, [string] $ConsumerBuild)
    if ($Spec.Toolchain -eq 'msvc')
    {
        return (Join-Path (Join-Path $ConsumerBuild $Spec.Config) 'install_consumer.exe')
    }
    return (Join-Path $ConsumerBuild 'install_consumer.exe')
}

# ---- 单格执行 ---------------------------------------------------------------
function Invoke-Cell
{
    param([object] $Spec)

    $cellDir       = Join-Path $WorkRoot $Spec.Name
    $productBuild  = Join-Path $cellDir 'product'
    $prefix        = Join-Path $cellDir 'install'
    $consumerBuild = Join-Path $cellDir 'consumer'
    $logDir        = Join-Path $cellDir 'logs'

    if (Test-Path -LiteralPath $cellDir)
    {
        Remove-Item -LiteralPath $cellDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $cellDir -Force | Out-Null

    Write-Host "`n=== cell: $($Spec.Name) ($($Spec.Toolchain) $($Spec.Config) $($Spec.Mode), profile=$($Spec.Profile)) ==="
    Add-Evidence ''
    Add-Evidence ("### cell: {0}" -f $Spec.Name)
    Add-Evidence ("  toolchain={0}  config={1}  mode={2}  profile={3}  monolithic={4}" -f
        $Spec.Toolchain, $Spec.Config, $Spec.Mode, $Spec.Profile, (Get-MonoFlag $Spec.Mode))
    Add-Evidence ("  product_build : {0}" -f $productBuild)
    Add-Evidence ("  install_prefix: {0}" -f $prefix)
    Add-Evidence ("  consumer_build: {0}" -f $consumerBuild)

    $result = [ordered]@{
        Cell              = $Spec.Name
        Toolchain         = $Spec.Toolchain
        Config            = $Spec.Config
        Mode              = $Spec.Mode
        Profile           = $Spec.Profile
        ConfigureExit     = $null
        BuildExit         = $null
        InstallExit        = $null
        ConsumerConfigure = $null
        ConsumerBuild     = $null
        ConsumerRun       = $null
        Result            = 'FAIL'
        Note              = ''
    }

    # profile 是"选中集合"的唯一真源：本格声明的期望必须与它逐项一致，
    # 否则本格直接判失败（驱动器与产品语义漂移时必须暴露，不能靠手抄放宽）
    $profileExpect = Get-ProfileExpectations $Spec.Profile
    Add-Evidence ("  profile enabled_plugins: {0}  ({1})" -f ($profileExpect.Names -join ', '), $profileExpect.Path)
    $expectMismatch = ($profileExpect.IdLog -ne $Spec.ExpectIdLog) -or ($profileExpect.Scalar -ne $Spec.ExpectScalar) -or ($profileExpect.Simd -ne $Spec.ExpectSimd)
    if ($expectMismatch)
    {
        $result.Note = 'cell 声明的期望与 profile 的 enabled_plugins 不一致'
        Add-Evidence '  <-- FAILED: 本格声明的期望与 profile 的 enabled_plugins 不一致'
        $script:Cells.Add([pscustomobject]$result)
        return
    }

    # ---- 1. 产品 configure ---------------------------------------------------
    $generatorArgs = Get-GeneratorArguments $Spec
    $configureArgs = @('-S', $EngineRoot, '-B', $productBuild) + $generatorArgs + @(
        "-DID_ASSEMBLY_PROFILE=$($Spec.Profile)",
        "-DID_MONOLITHIC_BUILD=$(Get-MonoFlag $Spec.Mode)",
        '-DID_BUILD_SANDBOX=OFF',
        '-DID_BUILD_BENCHMARK=OFF',
        '-DID_BUILD_ASSEMBLY_SMOKE=OFF'
    )
    if ($Spec.Toolchain -eq 'mingw')
    {
        $configureArgs += "-DCMAKE_BUILD_TYPE=$($Spec.Config)"
    }

    $configureStep = Invoke-Native -Exe $CMake -Arguments $configureArgs -WorkingDirectory $cellDir `
        -LogFile (Join-Path $logDir '01-configure.log')
    $result.ConfigureExit = $configureStep.Exit
    if ($configureStep.Exit -ne 0)
    {
        $result.Note = 'product configure failed'
        Add-Evidence ("  product configure: exit={0}  ({1}s)  <-- FAILED" -f (Format-ExitCode $configureStep.Exit), $configureStep.Seconds)
        Add-Evidence '  --- configure 输出尾部 ---'
        foreach ($line in (Get-Tail $configureStep.Output 60)) { Add-Evidence ("  | " + $line) }
        $script:Cells.Add([pscustomobject]$result)
        return
    }
    Add-Evidence ("  product configure: exit={0}  ({1}s)" -f (Format-ExitCode $configureStep.Exit), $configureStep.Seconds)

    # ---- 2. 产品 build -------------------------------------------------------
    $buildArgs = @('--build', $productBuild, '--parallel', '8')
    if ($Spec.Toolchain -eq 'msvc')
    {
        $buildArgs += @('--config', $Spec.Config)
    }
    $buildStep = Invoke-Native -Exe $CMake -Arguments $buildArgs -WorkingDirectory $cellDir `
        -LogFile (Join-Path $logDir '02-build.log')
    $result.BuildExit = $buildStep.Exit
    if ($buildStep.Exit -ne 0)
    {
        $result.Note = 'product build failed'
        Add-Evidence ("  product build: exit={0}  ({1}s)  <-- FAILED" -f (Format-ExitCode $buildStep.Exit), $buildStep.Seconds)
        Add-Evidence '  --- build 输出尾部 ---'
        foreach ($line in (Get-Tail $buildStep.Output 60)) { Add-Evidence ("  | " + $line) }
        $script:Cells.Add([pscustomobject]$result)
        return
    }
    Add-Evidence ("  product build: exit={0}  ({1}s)" -f (Format-ExitCode $buildStep.Exit), $buildStep.Seconds)

    # ---- 3. 安装到本格独占前缀 ----------------------------------------------
    $installArgs = @('--install', $productBuild, '--prefix', $prefix)
    if ($Spec.Toolchain -eq 'msvc')
    {
        $installArgs += @('--config', $Spec.Config)
    }
    $installStep = Invoke-Native -Exe $CMake -Arguments $installArgs -WorkingDirectory $cellDir `
        -LogFile (Join-Path $logDir '03-install.log')
    $result.InstallExit = $installStep.Exit
    if ($installStep.Exit -ne 0)
    {
        $result.Note = 'install failed'
        Add-Evidence ("  install: exit={0}  <-- FAILED" -f (Format-ExitCode $installStep.Exit))
        foreach ($line in (Get-Tail $installStep.Output 40)) { Add-Evidence ("  | " + $line) }
        $script:Cells.Add([pscustomobject]$result)
        return
    }
    Add-Evidence ("  install: exit={0}  ({1}s)" -f (Format-ExitCode $installStep.Exit), $installStep.Seconds)

    # ---- 4. 安装前缀内容与产品元数据 ----------------------------------------
    # IDBuildInfo.txt 是"本格构建身份"的唯一可追踪物：缺失时本格按未完成处理，不能只看退出码
    $buildInfoPath = Join-Path $productBuild 'IDBuildInfo.txt'
    if (-not (Test-Path -LiteralPath $buildInfoPath))
    {
        $result.Note = 'IDBuildInfo.txt 缺失'
        Add-Evidence ("  IDBuildInfo.txt 缺失：{0}  <-- FAILED" -f $buildInfoPath)
        $script:Cells.Add([pscustomobject]$result)
        return
    }
    Add-Evidence ("  IDBuildInfo.txt ({0}):" -f $buildInfoPath)
    foreach ($line in ((Read-TextOrEmpty $buildInfoPath) -split "`r?`n"))
    {
        if ($line.Trim().Length -gt 0) { Add-Evidence ("    " + $line) }
    }

    if ($Spec.Toolchain -eq 'msvc')
    {
        $configNote = '  注意：多配置生成器下 IDBuildInfo.txt 的 configuration 字段为空（CMAKE_BUILD_TYPE 未参与），真实配置由 --config 指定，本格 = {0}'
        Add-Evidence ($configNote -f $Spec.Config)
    }

    $manifestPath = Join-Path $prefix ("share\IDGameEngine\assemblies\{0}\ProductManifest.txt" -f $Spec.Profile)
    Add-Evidence ("  安装清单: {0}  exists={1}" -f $manifestPath, (Test-Path -LiteralPath $manifestPath))
    foreach ($line in (Get-SelectLines -Path $manifestPath -Pattern 'assembly_name|^mode|selected|slot '))
    {
        Add-Evidence ("    " + $line.Trim())
    }

    foreach ($line in (Get-InstalledListing $prefix)) { Add-Evidence ("  " + $line) }

    # 安装头完整性：产品入口头、生成头、逐插件公开头
    $headerChecks = @(
        (Join-Path $prefix 'include\IDGameEngine\IDGameEngine.hpp'),
        (Join-Path $prefix ("include\IDGameEngine\generated\{0}\IDAssemblyRuntime.generated.hpp" -f $Spec.Profile)),
        (Join-Path $prefix 'lib\cmake\IDGameEngine\IDGameEngineConfig.cmake'),
        (Join-Path $prefix 'lib\cmake\IDGameEngine\IDGameEngineTargets.cmake')
    )
    if ($Spec.ExpectScalar -or $Spec.ExpectSimd)
    {
        if ($Spec.ExpectScalar) { $headerChecks += (Join-Path $prefix 'include\IDGameEngine\plugins\IDMathScalar\IDMathScalarApi.hpp') }
        if ($Spec.ExpectSimd)   { $headerChecks += (Join-Path $prefix 'include\IDGameEngine\plugins\IDMathSIMD\IDMathSIMDApi.hpp') }
    }
    if ($Spec.ExpectIdLog) { $headerChecks += (Join-Path $prefix 'include\IDGameEngine\plugins\IDLog\IDLogApi.hpp') }
    foreach ($header in $headerChecks)
    {
        Add-Evidence ("  header/target 存在? {0}  {1}" -f (Test-Path -LiteralPath $header), $header)
    }

    # ---- 5. 独立消费工程 configure（find_package 在 target 之前）------------
    $consumerGeneratorArgs = Get-GeneratorArguments $Spec
    $consumerConfigureArgs = @('-S', $ConsumerSrc, '-B', $consumerBuild) + $consumerGeneratorArgs + @(
        "-DCMAKE_PREFIX_PATH=$prefix",
        "-DID_CONSUMER_EXPECT_MODE=$($Spec.Mode.ToUpperInvariant())",
        "-DID_CONSUMER_EXPECT_ASSEMBLY=$($Spec.Profile)"
    )
    if ($Spec.ExpectIdLog)  { $consumerConfigureArgs += '-DID_CONSUMER_EXPECT_IDLOG=ON' }
    if ($Spec.ExpectScalar) { $consumerConfigureArgs += '-DID_CONSUMER_EXPECT_MATH_SCALAR=ON' }
    if ($Spec.ExpectSimd)   { $consumerConfigureArgs += '-DID_CONSUMER_EXPECT_MATH_SIMD=ON' }
    if ($Spec.MathBackend)  { $consumerConfigureArgs += "-DID_CONSUMER_MATH_BACKEND=$($Spec.MathBackend)" }
    if ($Spec.Toolchain -eq 'mingw')
    {
        $consumerConfigureArgs += "-DCMAKE_BUILD_TYPE=$($Spec.Config)"
    }

    $consumerConfigureStep = Invoke-Native -Exe $CMake -Arguments $consumerConfigureArgs -WorkingDirectory $cellDir `
        -LogFile (Join-Path $logDir '04-consumer-configure.log')
    $result.ConsumerConfigure = $consumerConfigureStep.Exit
    if ($consumerConfigureStep.Exit -ne 0)
    {
        $result.Note = 'consumer configure failed'
        Add-Evidence ("  consumer configure: exit={0}  <-- FAILED" -f (Format-ExitCode $consumerConfigureStep.Exit))
        foreach ($line in (Get-Tail $consumerConfigureStep.Output 40)) { Add-Evidence ("  | " + $line) }
        $script:Cells.Add([pscustomobject]$result)
        return
    }
    Add-Evidence ("  consumer configure: exit={0}" -f (Format-ExitCode $consumerConfigureStep.Exit))
    foreach ($line in $consumerConfigureStep.Output)
    {
        if ($line -match 'install-consumer:|IDGameEngine:|RuntimeLibrary|PRODUCT_')
        {
            Add-Evidence ("    " + $line)
        }
    }

    # ---- 6. 消费工程 build ---------------------------------------------------
    $consumerBuildArgs = @('--build', $consumerBuild, '--parallel', '8')
    if ($Spec.Toolchain -eq 'msvc')
    {
        $consumerBuildArgs += @('--config', $Spec.Config)
    }
    $consumerBuildStep = Invoke-Native -Exe $CMake -Arguments $consumerBuildArgs -WorkingDirectory $cellDir `
        -LogFile (Join-Path $logDir '05-consumer-build.log')
    $result.ConsumerBuild = $consumerBuildStep.Exit
    if ($consumerBuildStep.Exit -ne 0)
    {
        $result.Note = 'consumer build failed'
        Add-Evidence ("  consumer build: exit={0}  <-- FAILED" -f (Format-ExitCode $consumerBuildStep.Exit))
        foreach ($line in (Get-Tail $consumerBuildStep.Output 60)) { Add-Evidence ("  | " + $line) }
        $script:Cells.Add([pscustomobject]$result)
        return
    }
    Add-Evidence ("  consumer build: exit={0}  ({1}s)" -f (Format-ExitCode $consumerBuildStep.Exit), $consumerBuildStep.Seconds)

    # 编译期证据：身份宏与 backend 选择、静态 C++ 运行时
    if ($Spec.Toolchain -eq 'msvc')
    {
        foreach ($line in (Get-MsvcDefinitions $consumerBuild $Spec.Config)) { Add-Evidence ("  consumer defines: " + $line) }
        foreach ($line in (Get-MsvcRuntimeEvidence $consumerBuild $Spec.Config)) { Add-Evidence ("  consumer CRT: " + $line) }
        $consumerExeForImports = Get-ConsumerExecutable $Spec $consumerBuild
        foreach ($line in (Get-MsvcDllImports $consumerExeForImports)) { Add-Evidence ("  consumer import: " + $line) }
    }
    else
    {
        foreach ($line in (Get-MingwDefinitions $consumerBuild)) { Add-Evidence ("  consumer defines: " + $line) }
        foreach ($line in (Get-MingwLinkEvidence $consumerBuild)) { Add-Evidence ("  consumer CRT: " + $line) }
        $consumerExeForImports = Get-ConsumerExecutable $Spec $consumerBuild
        foreach ($line in (Get-MingwDllImports $consumerExeForImports))
        {
            Add-Evidence ("  consumer import: " + $line)
        }
    }

    foreach ($line in (Get-WholeArchiveEvidence $Spec $consumerBuild)) { Add-Evidence ("  consumer link: " + $line) }

    # ---- 7. 运行消费程序：外部退出码 + 关键输出 ------------------------------
    $consumerExe = Get-ConsumerExecutable $Spec $consumerBuild
    if (-not (Test-Path -LiteralPath $consumerExe))
    {
        $result.Note = "consumer exe 不存在：$consumerExe"
        Add-Evidence ("  consumer run: <exe missing> {0}" -f $consumerExe)
        $script:Cells.Add([pscustomobject]$result)
        return
    }

    $exeInfo = Get-Item -LiteralPath $consumerExe
    Add-Evidence ("  consumer exe: size={0}  last_write={1}" -f $exeInfo.Length, $exeInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))

    $savedPath = $env:PATH
    $env:PATH  = (Join-Path $prefix 'bin') + ';' + $MinGWBin + ';' + $savedPath
    try
    {
        $runStep = Invoke-Native -Exe $consumerExe -Arguments @() -WorkingDirectory $cellDir `
            -LogFile (Join-Path $logDir '06-consumer-run.log')
    }
    finally
    {
        $env:PATH = $savedPath
    }
    $result.ConsumerRun = $runStep.Exit
    Add-Evidence ("  consumer run: exit={0}  cwd={1}  PATH 前置<prefix>\bin 与 MinGW bin" -f (Format-ExitCode $runStep.Exit), $cellDir)
    foreach ($line in $runStep.Output) { Add-Evidence ("  | " + $line) }

    if ($runStep.Exit -eq 0)
    {
        $result.Result = 'PASS'
    }
    else
    {
        $result.Note = 'consumer run returned non-zero'
    }

    Add-Evidence ("  产物: product_out={0}  prefix_bin={1}  consumer_exe={2}" -f
        (Join-Path $productBuild ("out\" + $Spec.Mode)), (Join-Path $prefix 'bin'), $consumerExe)

    $script:Cells.Add([pscustomobject]$result)
}

# ---- 反例对照：编译期/配置期的负向判据 --------------------------------------
function Invoke-NegativeControls
{
    param([string] $MathBothCellName, [string] $MsvcCellName)

    Add-Evidence ''
    Add-Evidence '### 编译期约束的对照（正向两选一 + 负向不给选择 + find_package 顺序）'

    # 反例 1：A+B 产品不给显式选择 -> 消费方必须编译失败（#error）
    $mathBothDir = Join-Path $WorkRoot $MathBothCellName
    $prefix      = Join-Path $mathBothDir 'install'
    if (Test-Path -LiteralPath $prefix)
    {
        $buildDir = Join-Path $mathBothDir 'consumer-no-choice'
        $configureArgs = @('-S', $ConsumerSrc, '-B', $buildDir, '-G', 'MinGW Makefiles', "-DCMAKE_CXX_COMPILER=$Gcc",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$prefix",
            '-DID_CONSUMER_EXPECT_MODE=SHARED', '-DID_CONSUMER_EXPECT_ASSEMBLY=math-both',
            '-DID_CONSUMER_EXPECT_MATH_SCALAR=ON', '-DID_CONSUMER_EXPECT_MATH_SIMD=ON')
        $cfgStep = Invoke-Native -Exe $CMake -Arguments $configureArgs -WorkingDirectory $mathBothDir `
            -LogFile (Join-Path $mathBothDir 'logs\nc1-configure.log')
        Add-Evidence ''
        Add-Evidence '  反例 1：A+B 产品 + 未给 -DID_CONSUMER_MATH_BACKEND'
        Add-Evidence ("    configure: exit={0}（期望 0）" -f (Format-ExitCode $cfgStep.Exit))
        if ($cfgStep.Exit -eq 0)
        {
            $buildStep = Invoke-Native -Exe $CMake -Arguments @('--build', $buildDir, '--parallel', '4') `
                -WorkingDirectory $mathBothDir -LogFile (Join-Path $mathBothDir 'logs\nc1-build.log')
            Add-Evidence ("    build: exit={0}（期望非 0）" -f (Format-ExitCode $buildStep.Exit))
            foreach ($line in $buildStep.Output)
            {
                if ($line -match 'error:|错误|ID_CONSUMER_MATH_BACKEND') { Add-Evidence ("    | " + $line) }
            }
        }
    }
    else
    {
        Add-Evidence ("  反例 1 跳过：{0} 的前缀不存在（该格未跑）" -f $MathBothCellName)
    }

    # 对照：同一个 A+B 产品显式选择 scalar（与主格选择 simd 对称，证明两个方向都可用）
    if (Test-Path -LiteralPath $prefix)
    {
        $buildDir = Join-Path $mathBothDir 'consumer-choice-scalar'
        $configureArgs = @('-S', $ConsumerSrc, '-B', $buildDir, '-G', 'MinGW Makefiles', "-DCMAKE_CXX_COMPILER=$Gcc",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$prefix",
            '-DID_CONSUMER_EXPECT_MODE=SHARED', '-DID_CONSUMER_EXPECT_ASSEMBLY=math-both',
            '-DID_CONSUMER_EXPECT_MATH_SCALAR=ON', '-DID_CONSUMER_EXPECT_MATH_SIMD=ON',
            '-DID_CONSUMER_MATH_BACKEND=scalar')
        $cfgStep = Invoke-Native -Exe $CMake -Arguments $configureArgs -WorkingDirectory $mathBothDir `
            -LogFile (Join-Path $mathBothDir 'logs\nc1b-configure.log')
        $buildStep = Invoke-Native -Exe $CMake -Arguments @('--build', $buildDir, '--parallel', '4') `
            -WorkingDirectory $mathBothDir -LogFile (Join-Path $mathBothDir 'logs\nc1b-build.log')
        $savedPath = $env:PATH
        $env:PATH  = (Join-Path $prefix 'bin') + ';' + $MinGWBin + ';' + $savedPath
        try
        {
            $runStep = Invoke-Native -Exe (Join-Path $buildDir 'install_consumer.exe') -Arguments @() `
                -WorkingDirectory $mathBothDir -LogFile (Join-Path $mathBothDir 'logs\nc1b-run.log')
        }
        finally
        {
            $env:PATH = $savedPath
        }
        Add-Evidence ''
        Add-Evidence '  对照：同一 A+B 产品显式选择 scalar（-DID_CONSUMER_MATH_BACKEND=scalar）'
        Add-Evidence ("    configure: exit={0}  build: exit={1}  run: exit={2}" -f
            (Format-ExitCode $cfgStep.Exit), (Format-ExitCode $buildStep.Exit), (Format-ExitCode $runStep.Exit))
        foreach ($line in $runStep.Output)
        {
            if ($line -match 'Math|summary') { Add-Evidence ("    | " + $line) }
        }
    }

    # 反例 2：MSVC 上把 target 建在 find_package 之前 -> 顺序自检必须报错
    $msvcDir = Join-Path $WorkRoot $MsvcCellName
    $msvcPrefix = Join-Path $msvcDir 'install'
    if (Test-Path -LiteralPath $msvcPrefix)
    {
        $buildDir = Join-Path $msvcDir 'consumer-wrong-order'
        $configureArgs = @('-S', $ConsumerSrc, '-B', $buildDir, '-G', $VsGenerator, '-A', 'x64',
            "-DCMAKE_GENERATOR_INSTANCE=$VsInstance", "-DCMAKE_PREFIX_PATH=$msvcPrefix",
            '-DID_CONSUMER_EXPECT_MODE=SHARED', '-DID_CONSUMER_EXPECT_ASSEMBLY=ship',
            '-DID_CONSUMER_EXPECT_IDLOG=ON', '-DID_CONSUMER_EXPECT_MATH_SCALAR=ON',
            '-DID_CONSUMER_DEMO_WRONG_ORDER=ON')
        $cfgStep = Invoke-Native -Exe $CMake -Arguments $configureArgs -WorkingDirectory $msvcDir `
            -LogFile (Join-Path $msvcDir 'logs\nc2-configure.log')
        Add-Evidence ''
        Add-Evidence '  反例 2：MSVC 上 target 早于 find_package 创建'
        Add-Evidence ("    configure: exit={0}（期望非 0）" -f (Format-ExitCode $cfgStep.Exit))
        foreach ($line in $cfgStep.Output)
        {
            if ($line -match 'order-check|顺序错误|find_package|CMake Error') { Add-Evidence ("    | " + $line) }
        }
    }
    else
    {
        Add-Evidence ("  反例 2 跳过：{0} 的前缀不存在（该格未跑）" -f $MsvcCellName)
    }
}

# ---- 主流程 -----------------------------------------------------------------
$hostInfo = @(
    "host_os      : $([System.Environment]::OSVersion.VersionString)",
    "powershell   : $($PSVersionTable.PSVersion)",
    "cmake        : $((& $CMake --version 2>&1)[0])",
    "g++          : $((& $Gcc --version 2>&1)[0])",
    "vs_instance  : $VsInstance ($VsGenerator)"
) | ForEach-Object { $_ -replace "`r", '' }

$started = Get-Date
Add-Evidence 'IDGameEngine 安装消费矩阵证据（Benchmark/validation/install-consumer）'
Add-Evidence '本文件由 RunInstallConsumerMatrix.ps1 生成，勿手改；每次运行整份覆盖。'
Add-Evidence ''
Add-Evidence ("generated    : {0}" -f $started.ToString('yyyy-MM-dd HH:mm:ss'))
Add-Evidence ("engine_root  : {0}" -f $EngineRoot)
Add-Evidence ("consumer_src : {0}" -f $ConsumerSrc)
Add-Evidence ("work_root    : {0}" -f $WorkRoot)
foreach ($line in $hostInfo) { Add-Evidence $line }
Add-Evidence ''
Add-Evidence '口径：每格产品独立构建目录 + 独立安装前缀；消费工程只经 find_package(IDGameEngine) 消费；'
Add-Evidence '     运行期把 <prefix>\bin 前置到 PATH（Windows DLL 查找），不改产品、不改安装前缀内容。'
Add-Evidence '     profile=ship 为交付 profile（显式列出 ID.Log 与 ID.Math.Scalar）；三组合格用 math-scalar / math-simd / math-both。'

foreach ($spec in $selectedCells)
{
    Invoke-Cell -Spec $spec
}

# 反例只在相关格都跑过时执行
$ranMathBoth = @($script:Cells | Where-Object { $_.Cell -eq 'mingw-release-shared-math-both' }).Count -gt 0
$ranMsvc     = @($script:Cells | Where-Object { $_.Cell -eq 'msvc-release-shared' }).Count -gt 0
if ($ranMathBoth -or $ranMsvc)
{
    $mathBothName = 'mingw-release-shared-math-both'
    $msvcName     = 'msvc-release-shared'
    Invoke-NegativeControls -MathBothCellName $mathBothName -MsvcCellName $msvcName
}

# ---- 汇总表 -----------------------------------------------------------------
Add-Evidence ''
Add-Evidence '### 汇总（逐格结果）'
Add-Evidence '| cell | toolchain | config | mode | profile | configure | build | install | consumer-config | consumer-build | consumer-run | result |'
Add-Evidence '|---|---|---|---|---|---|---|---|---|---|---|---|'
foreach ($entry in $script:Cells)
{
    Add-Evidence ("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} | {11} |" -f
        $entry.Cell, $entry.Toolchain, $entry.Config, $entry.Mode, $entry.Profile,
        $entry.ConfigureExit, $entry.BuildExit, $entry.InstallExit,
        $entry.ConsumerConfigure, $entry.ConsumerBuild, $entry.ConsumerRun, $entry.Result)
}

$totalCells = $script:Cells.Count
$passed     = @($script:Cells | Where-Object { $_.Result -eq 'PASS' }).Count
Add-Evidence ''
Add-Evidence ("summary: {0} / {1} 格 PASS" -f $passed, $totalCells)

$finished = Get-Date
Add-Evidence ("finished     : {0}（耗时 {1} 分钟）" -f $finished.ToString('yyyy-MM-dd HH:mm:ss'),
    [math]::Round(($finished - $started).TotalMinutes, 1))

[IO.File]::WriteAllText($EvidenceFile, (($script:Evidence -join "`r`n") + "`r`n"),
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host ("evidence: {0}" -f $EvidenceFile)
Write-Host ("summary : {0} / {1} 格 PASS" -f $passed, $totalCells)

if (-not $KeepWork)
{
    Write-Host ("work dir kept for inspection: {0}（加 -KeepWork 可静默保留；删除由用户/下次运行负责）" -f $WorkRoot)
}

if ($passed -ne $totalCells)
{
    exit 1
}
exit 0
