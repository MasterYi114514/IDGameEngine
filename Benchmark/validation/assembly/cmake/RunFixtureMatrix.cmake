# 组装 fixture 矩阵驱动器（cmake -P）
#
# 职责：对 fixtures/<case> 逐个做**真实执行**的判定，不接受静态推断。
#   1. 每个 case 是一个独立的 CMake 探针工程（cmake/FixtureProbe.cmake），由本驱动器
#      `cmake -S fixtures/<case> -B <work>/<case>` 配置；探针内部 include() 复用
#      Assemble/cmake 的 IDReadCatalog / IDResolveSelection / IDGenerateAssembly，不复制解析逻辑；
#   2. **正例**：configure 必须 exit=0，然后对**生成物**逐条断言（证据头计数、canonical 段、
#      group_allows_multi 赋值、<Target>Matched.generated.hpp 的 availability 宏）；再编译
#      生成的 IDAssemblyValidate.generated.cpp（C++ 静态门禁必须通过）并运行指纹探针取
#      真实的编译期常量 kProductFingerprint；
#   3. **configure 期反例**：configure 必须 exit!=0，且输出里**恰好 1 条**类别诊断行，
#      其类别必须等于该 case 的期望类别（按具体类别断言，不看退出码了事）：
#      判定保留 `EXPECTED:<类别>` 记录行，任何"类别不符 / 缺失 / 出现两条 / 只看到退出码"
#      都必须被拒绝。判定器自身由第 0 节的自检证明（喂正确与错误的合成输出）。
#   4. **编译期反例**：configure 必须通过，随后编译生成的校验 TU 必须失败，
#      且恰好 1 处 `ID_ASSEMBLY_ERROR:<类别>` 标记（环）或恰好 1 条 `EXPECTED:<类别>` 标记
#      （传递身份宏误判）；
#   5. 等价重排（catalog / profile / 候选顺序）必须得到同一个 kProductFingerprint 与同一份
#      canonical 摘要；
#   6. 报告逐 case 记录 **configure 退出码 / 编译退出码 / 判定结果 / `EXPECTED:<类别>` 行**，
#      并在 [4] 声明当前资产形状的**未覆盖清单（含应覆盖方）**；逐 case 的机器可读记录另写
#      `out/fixture-matrix-cases.txt`，供 cmake/CompareFixtureMatrixRuns.cmake 汇总成四配置对照表。
#
# 口径与边界：
#   - 本资产只做**正确性/结构**校验：不测量任何时间、不产生性能评级、不写官方
#     `<benchmark>/report.txt`；报告与原始日志**只落本构建目录**
#     （`<build>/Benchmark/validation/assembly/out/`），运行期不再写 `validation/baseline/raw/**`
#     （历史目录只作既往证据保留，不删除也不再新增写入）。
#   - 不做 target 创建、不产出插件二进制、不跑运行期生命周期：那些属既有
#     IDAssemblyValidationRun（P2 批次）的职责，两者互补而不互相替代。
#   - 编译用例的 `-DID_USE_PLUGIN_<TOKEN>=1` 来自探针汇报的**真实 PUBLIC 链接边**的传递闭包
#     （不是手工挑选），因为本资产不创建 target、拿不到 CMake usage requirement；
#     真实构建里这些宏由上游 target 的 PUBLIC usage requirement 传播，由 P2 批次核对 flags.make。
#
# 参数（由 validation/assembly/CMakeLists.txt 透传）：
#   ASSEMBLY_DIR ENGINE_SOURCE_DIR WORK_DIR REPORT_DIR RAW_LOG_DIR GENERATOR
#   GENERATOR_PLATFORM GENERATOR_INSTANCE BUILD_TYPE RUN_CONFIG CXX_COMPILER CXX_COMPILER_ID
#
# 退出码：0 = 全部 case 按期望通过；1 = 至少一条判定不符（问题清单在报告末尾）。
# 正常入口是 RunFixtureMatrixEntry.cmake（它转发参数、捕获并回填本脚本的**外部观察退出码**）；
# 直接 `cmake -P` 调用本脚本只用于调试，此时报告头的 driver exit code 保持 PENDING 占位值。

foreach(required_var ASSEMBLY_DIR ENGINE_SOURCE_DIR WORK_DIR REPORT_DIR RAW_LOG_DIR CXX_COMPILER CXX_COMPILER_ID)
    if("${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "RunFixtureMatrix: 缺少参数 ${required_var}")
    endif()
endforeach()

if(NOT EXISTS "${ASSEMBLY_DIR}/cmake/FixtureProbe.cmake")
    message(FATAL_ERROR "RunFixtureMatrix: 找不到探针模块 ${ASSEMBLY_DIR}/cmake/FixtureProbe.cmake")
endif()
if(NOT EXISTS "${ENGINE_SOURCE_DIR}/ID/include/Kernel/PluginCompileTime.hpp")
    message(FATAL_ERROR "RunFixtureMatrix: 找不到 ID 头 ${ENGINE_SOURCE_DIR}/ID/include/Kernel/PluginCompileTime.hpp")
endif()
if(NOT EXISTS "${CXX_COMPILER}")
    message(FATAL_ERROR "RunFixtureMatrix: CXX 编译器路径不可用：${CXX_COMPILER}")
endif()

set(ID_INCLUDE_DIR    "${ENGINE_SOURCE_DIR}/ID/include")
set(ASSEMBLE_DIR      "${ENGINE_SOURCE_DIR}/Assemble")
set(FIXTURE_SOURCE_DIR "${ASSEMBLY_DIR}/src")
set(CASE_DIR          "${ASSEMBLY_DIR}/fixtures")

file(MAKE_DIRECTORY "${WORK_DIR}" "${REPORT_DIR}" "${RAW_LOG_DIR}")

# 报告形状版本：报告字段集合变化时递增，便于"同一份报告格式"跨配置对照
set(ID_FIXTURE_REPORT_VERSION "fixture-matrix-r2")

# 未覆盖清单：当前资产形状没有覆盖、且不能靠"再看一遍报告"补上的判定类别。
# 逐条写清**应覆盖方**：本资产只声明缺口，不冒充已覆盖，也不在本轮越权新增 fixture。
set(ID_FIXTURE_UNCOVERED_TEXT [=[
  1) profile 级负例（profile 自身的 schema / 字段 / 引用错误）—— 未覆盖
     本资产只驱动 fixture 清单 + 探针生成的临时 profile，没有构造「缺 enabled_plugins、
     enabled_plugins 引用未知插件、profile 字段类型错误」这类负例。
     应覆盖方：assemble-developer（Assemble 解析器 owner，A02/A04 范围）。
     若要求在 B01 资产内补 fixture，需主 Agent 授权新增 fixture（本轮范围是证据与报告格式整改）。
  2) DuplicateCapability / SlotConflict / RevisionMismatch / SchemaMismatch —— 未覆盖
     四者是解析/校验器的既有诊断类别，但本资产没有触发它们的 fixture。
     应覆盖方：assemble-developer 给出各类别的最小合法清单形状（或确认只能由真实产品 catalog
     触发），再由 B01 在 validation/assembly/fixtures/** 增补；本轮未新增 fixture。
  3) 容量上限类别（TooManyActive / TooManyRequirements / TooManyCandidates / TooManyCatalog /
     TooManyMatchedRecords / TooManyEdges）—— 未覆盖
     这些是 D0-09 冻结的预算错误（128 / 32 / 16 / 16256 量级），构造需要大规模清单。
     应覆盖方：assemble-developer（容量口径）+ benchmark-developer（规模用例形状），属 A03/A04；
     本资产只做小规模语义判定，不替代容量验收。
  4) schema2 混写正例（同一 schema2 清单里既有字符串单项又有数组 OR 组，且全部命中）—— 未覆盖
     现有 schema2 正例只有纯数组 OR（p02–p04），schema2 AND 只有反例（n03：两个字符串项缺一）。
     应覆盖方：A02 契约 owner（assemble-developer）确认混写语义后补 fixture。
  5) CandidateCapabilityMismatch（命中候选未真正提供它自己声明的能力，D0-01 第二项校验）—— 未覆盖
     应覆盖方：assemble-developer 提供触发形状（候选声明能力 X、清单未提供 X），再由 B01 补 fixture。
  边界内不算缺口：Evidence* / RuntimeBindingMismatch 等类别由既有 IDAssemblyValidationRun
  （P2 批次，改坏生成物副本）覆盖，不属本资产形状；运行期生命周期、真实 PUBLIC 链接边与产物
  集合同样归 P2，不由本资产冒充。
]=])

# 原始日志目录归属：一个日志目录绑定唯一构建目录（沿用 P2 批次的 batch-owner 口径）
set(owner_path "${RAW_LOG_DIR}/batch-owner.txt")
if(EXISTS "${owner_path}")
    file(STRINGS "${owner_path}" owner_lines)
    if(owner_lines)
        list(GET owner_lines 0 previous_work_dir)
        if(NOT "${previous_work_dir}" STREQUAL "${WORK_DIR}")
            message(FATAL_ERROR
                "组装 fixture 原始日志目录 ${RAW_LOG_DIR} 已属于另一个工作目录：\n"
                "  已记录：${previous_work_dir}\n  当前  ：${WORK_DIR}\n"
                "同一目录不允许被两个构建目录共用。请换构建目录名，或人工确认后删除归属文件。")
        endif()
    endif()
endif()
file(WRITE "${owner_path}" "${WORK_DIR}\n")

# ---------------------------------------------------------------------------
# 通用工具
# ---------------------------------------------------------------------------

# fixture_run(<out_rc> <log_stem> <command...>)：在 WORK_DIR 下执行，stdout/stderr 分开落盘
function(fixture_run out_rc log_stem)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE run_rc
        OUTPUT_FILE "${log_stem}.out"
        ERROR_FILE  "${log_stem}.err")
    set(${out_rc} "${run_rc}" PARENT_SCOPE)
endfunction()

# fixture_read_logs(<out_text> <log_stem>)：把 stdout 与 stderr 合成一段文本
function(fixture_read_logs out_text log_stem)
    set(text "")
    foreach(suffix out err)
        if(EXISTS "${log_stem}.${suffix}")
            file(READ "${log_stem}.${suffix}" part)
            string(APPEND text "${part}\n")
        endif()
    endforeach()
    set(${out_text} "${text}" PARENT_SCOPE)
endfunction()

# 组装期诊断类别的完整清单（顺序与 ID::PluginCompileTime::PluginError 一致，另加 schema2 目标类别）。
# 只用于"某一行是不是类别诊断"的判定：行首空白 + 类别名 + 一个不属于名字的字节（实现用全角冒号 '：'）。
set(ID_FIXTURE_KNOWN_CATEGORIES
    InvalidName InvalidTargetName InvalidNamespace InvalidDeclaration SchemaMismatch
    TooManyCatalog TooManyRequirements TooManyCandidates TooManyProvides
    EmptyRequirementGroup DuplicateCandidate SelfDependency InvalidCapability
    InvalidRevision InvalidRevisionRange InvalidSlot InterfaceLifecycleMismatch
    ContractRequirementMismatch ContractCandidateMismatch DuplicatePlugin NameHashCollision
    TargetMacroConflict NamespaceConflict UnknownCandidate CandidateCapabilityMismatch
    TooManyActive InvalidActive UnknownActive DuplicateActive MissingProvider AmbiguousProvider
    RevisionMismatch DuplicateCapability SlotConflict Cycle
    EvidenceSelectionMismatch EvidenceTargetMismatch EvidenceMacroMismatch EvidenceIdentityMismatch
    EvidenceProviderMismatch EvidenceEdgeMismatch EvidenceSlotMismatch RuntimeBindingMismatch
    TooManyMatchedRecords TooManyEdges NestedRequirementGroup ManifestTypeMismatch)

# fixture_count_category(<out_count> <text> <category>)：统计"类别诊断行"条数
function(fixture_count_category out_count text category)
    string(REPLACE "\r" "" flat "${text}")
    string(REPLACE "\n" ";" lines "${flat}")
    set(hits 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^ *${category}[^A-Za-z0-9_]")
            math(EXPR hits "${hits} + 1")
        endif()
    endforeach()
    set(${out_count} "${hits}" PARENT_SCOPE)
endfunction()

# fixture_observed_categories(<out_list> <text>)：返回 "类别=条数" 列表（只列出现过的已知类别）
function(fixture_observed_categories out_list text)
    set(observed "")
    foreach(category IN LISTS ID_FIXTURE_KNOWN_CATEGORIES)
        fixture_count_category(count "${text}" "${category}")
        if(count GREATER 0)
            list(APPEND observed "${category}=${count}")
        endif()
    endforeach()
    set(${out_list} "${observed}" PARENT_SCOPE)
endfunction()

# fixture_judge_category(<out_verdict> <out_detail> <text> <expected>)
#   verdict: ok | category-mismatch | category-count-not-one | no-category
function(fixture_judge_category out_verdict out_detail text expected)
    fixture_observed_categories(observed "${text}")
    list(LENGTH observed observed_count)
    if(observed_count EQUAL 0)
        set(${out_verdict} "no-category" PARENT_SCOPE)
        set(${out_detail} "输出里没有任何类别诊断行（只有退出码不能作为判据）" PARENT_SCOPE)
        return()
    endif()
    if(NOT observed_count EQUAL 1)
        string(REPLACE ";" " " observed_text "${observed}")
        set(${out_verdict} "category-count-not-one" PARENT_SCOPE)
        set(${out_detail} "期望恰好 1 条类别诊断行，实际 ${observed_count} 种：${observed_text}" PARENT_SCOPE)
        return()
    endif()
    list(GET observed 0 only_one)
    if(NOT only_one STREQUAL "${expected}=1")
        set(${out_verdict} "category-mismatch" PARENT_SCOPE)
        set(${out_detail} "期望 ${expected}（恰好 1 条），实际 ${only_one}" PARENT_SCOPE)
        return()
    endif()
    set(${out_verdict} "ok" PARENT_SCOPE)
    set(${out_detail} "${expected}=1" PARENT_SCOPE)
endfunction()

# fixture_count_diagnostic_marker(<out_count> <text> <marker>)：统计**诊断行**里某标记出现的条数
#
# 只数诊断行（行内含 "error"）：GCC/Clang 会把出错的源码行整行回显，回显里同样包含标记文本，
# 若按全文计数就会把一条断言数成两条（实测：`#error "EXPECTED:...` 会得到 2）。
# 因此"恰好 1 条"的口径以诊断行为准，而不是原始子串出现次数。
function(fixture_count_diagnostic_marker out_count text marker)
    string(REPLACE "\r" "" flat "${text}")
    string(REPLACE "\n" ";" lines "${flat}")
    set(hits 0)
    foreach(line IN LISTS lines)
        # 只认编译器的诊断行形式（`: error:` / `: fatal error`），排除源码行回显
        # ——回显里同样会出现 "error" 字样（例如 `#    error "..."`），按全文计数会翻倍。
        if(line MATCHES ": *(fatal )?error" AND line MATCHES "${marker}")
            math(EXPR hits "${hits} + 1")
        endif()
    endforeach()
    set(${out_count} "${hits}" PARENT_SCOPE)
endfunction()

# fixture_probe_value(<out_value> <text> <key>)：取 "FIXTURE-PROBE <key> <value>" 的 value
function(fixture_probe_value out_value text key)
    string(REPLACE "\r" "" flat "${text}")
    string(REPLACE "\n" ";" lines "${flat}")
    set(value "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^-- FIXTURE-PROBE ${key} (.*)$")
            set(value "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out_value} "${value}" PARENT_SCOPE)
endfunction()

# fixture_probe_all(<out_list> <text> <key>)：取所有匹配行的 value（$1 起）
function(fixture_probe_all out_list text key)
    string(REPLACE "\r" "" flat "${text}")
    string(REPLACE "\n" ";" lines "${flat}")
    set(values "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^-- FIXTURE-PROBE ${key} (.*)$")
            list(APPEND values "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out_list} "${values}" PARENT_SCOPE)
endfunction()

# fixture_in_list(<out_found> <list> <value>)
function(fixture_in_list out_found list_value value)
    set(found FALSE)
    foreach(item IN LISTS list_value)
        if(item STREQUAL "${value}")
            set(found TRUE)
        endif()
    endforeach()
    set(${out_found} "${found}" PARENT_SCOPE)
endfunction()

set(problems "")
set(notes "")

# ---------------------------------------------------------------------------
# 0. 判定器自检：合成的 configure 输出必须被正确接受/拒绝
# ---------------------------------------------------------------------------
# 口径：正例用"完全正确"的输出；其余 4 条必须被拒绝（类别不符 / 出现两条 / 没有类别 / 只有退出码）。
set(selfcheck_dir "${WORK_DIR}/judge-selfcheck")
file(MAKE_DIRECTORY "${selfcheck_dir}")

set(selfcheck_good
"CMake Error at F:/engine/Assemble/cmake/IDResolveSelection.cmake:252 (message):\n"
"  MissingProvider：FX.Consumer.Missing 的需求组 0（契约标签 FX.Core）在选中集合内没有候选；\n"
"  不自动启用缺失的依赖\n"
"Call Stack (most recent call first):\n"
"  F:/engine/Benchmark/validation/assembly/fixtures/n02_schema1_missing/CMakeLists.txt:9 (id_fixture_probe)\n")
set(selfcheck_wrong "\n  AmbiguousProvider：命中 2 个候选\n")
set(selfcheck_two "\n  MissingProvider：\n  DuplicateCandidate：\n")
set(selfcheck_none "CMake Error at x.cmake:1 (message):\n  清单缺少字段 name：plugin.json\n")
set(selfcheck_exitcode_only "")

set(selfcheck_cases
    "good:MissingProvider:ok"
    "wrong:MissingProvider:category-mismatch"
    "two:MissingProvider:category-count-not-one"
    "none:MissingProvider:no-category"
    "exitcode-only:MissingProvider:no-category")

set(selfcheck_report "")
foreach(entry IN LISTS selfcheck_cases)
    string(REPLACE ":" ";" fields "${entry}")
    list(GET fields 0 case_name)
    list(GET fields 1 expected)
    list(GET fields 2 want_verdict)
    set(text "${selfcheck_${case_name}}")
    fixture_judge_category(verdict detail "${text}" "${expected}")
    if(verdict STREQUAL want_verdict)
        set(state "as-expected")
    else()
        set(state "UNEXPECTED")
        string(APPEND problems "judge-selfcheck ${case_name}：期望 verdict=${want_verdict}，实际 ${verdict}（${detail}）\n")
    endif()
    string(APPEND selfcheck_report "${case_name}: verdict=${verdict} expect=${want_verdict} ${state} ${detail}\n")
endforeach()
# 编译期标记判定的自检：`恰好 1 条` 必须能区分"1 条 / 2 条 / 0 条 / 只在源码回显里出现"，
# 否则环与传递身份宏这两个编译级反例就是空转。
set(marker_case_names  "one" "two" "none" "echo-only")
set(marker_case_texts
    "x.cpp(29): error: #error \"EXPECTED:Foo - detail\"\n   29 | #    error \"EXPECTED:Foo\"\n"
    "x.cpp(1): error: #error \"EXPECTED:Foo first\"\nx.cpp(2): error: #error \"EXPECTED:Foo second\"\n"
    "x.cpp(1): warning: unrelated diagnostic\n"
    "   29 | #    error \"EXPECTED:Foo\"\n")
set(marker_expected_counts 1 2 0 0)
set(marker_index 0)
foreach(marker_case IN LISTS marker_case_names)
    list(GET marker_case_texts ${marker_index} marker_text)
    list(GET marker_expected_counts ${marker_index} marker_expect)
    math(EXPR marker_index "${marker_index} + 1")
    fixture_count_diagnostic_marker(marker_hits "${marker_text}" "EXPECTED:Foo")
    if(marker_hits EQUAL marker_expect)
        set(marker_state "as-expected")
    else()
        set(marker_state "UNEXPECTED")
        string(APPEND problems "marker-selfcheck ${marker_case}：期望 count=${marker_expect}，实际 ${marker_hits}\n")
    endif()
    string(APPEND selfcheck_report "marker-${marker_case}: count=${marker_hits} expect=${marker_expect} ${marker_state}\n")
endforeach()

file(WRITE "${selfcheck_dir}/judge-selfcheck.txt" "${selfcheck_report}")
message(STATUS "fixture-matrix[selfcheck] ${selfcheck_report}")

# ---------------------------------------------------------------------------
# 1. case 表
# ---------------------------------------------------------------------------
# 每项：<case>|<kind>|<expected>|<checks 逗号分隔>
#   kind：product  正例（configure 通过 + 生成物断言 + 校验 TU 通过 + 指纹探针）
#         parser   configure 期反例（按类别断言）
#         cycle    环：configure 通过，校验 TU 必须按 Cycle 拒绝
#         nocycle  环的合法对照：configure 通过，校验 TU 必须通过
#         macros   传递身份宏误判：configure 通过，正向对照 TU 通过、负向 TU 必须失败
#   checks（仅 product）：selected= / bindings= / edges= / slots= / provisions= /
#         multi=<slot>:<group>:<0|1>（1 = 生成头里必须有该 allows_multi 赋值；0 = 必须没有）/
#         avail=<TOKEN>:<group>:<count> / member=<TOKEN>:<group>:<PROVIDER> /
#         nomember=<TOKEN>:<group>:<PROVIDER> / canonbind=<canonical-binding 行数> /
#         fpeq=<另一个 case>（指纹必须相等）
set(id_fixture_cases
    "p01_schema1_single|product|pass|selected=2,bindings=1,edges=1,slots=0,provisions=1,multi=0:0:0,avail=IDFXCONSUMERSINGLE:0:1,member=IDFXCONSUMERSINGLE:0:IDFXCORE,nomember=IDFXCONSUMERSINGLE:0:IDFXALT,canonbind=1"
    "p02_schema2_or_a_only|product|pass|bindings=1,edges=1,multi=0:0:1,avail=IDFXCONSUMEROR:0:1,member=IDFXCONSUMEROR:0:IDFXCORE,nomember=IDFXCONSUMEROR:0:IDFXALT,canonbind=1"
    "p03_schema2_or_b_only|product|pass|bindings=1,edges=1,multi=0:0:1,avail=IDFXCONSUMEROR:0:1,member=IDFXCONSUMEROR:0:IDFXALT,nomember=IDFXCONSUMEROR:0:IDFXCORE,canonbind=1"
    "p04_schema2_or_both|product|pass|bindings=2,edges=2,multi=0:0:1,avail=IDFXCONSUMEROR:0:2,member=IDFXCONSUMEROR:0:IDFXCORE,member=IDFXCONSUMEROR:0:IDFXALT,canonbind=2"
    "p05_cross_group_same_provider|product|pass|bindings=2,edges=1,slots=0,multi=0:0:0,multi=0:1:1,avail=IDFXCONSUMERCROSS:0:1,avail=IDFXCONSUMERCROSS:1:1,member=IDFXCONSUMERCROSS:1:IDFXCORE,canonbind=2"
    "p06_reorder_catalog|product|pass|bindings=2,edges=2,multi=0:0:1,avail=IDFXCONSUMEROR:0:2,canonbind=2,fpeq=p04_schema2_or_both"
    # p07 的 profile 顺序被打乱（consumer 排在第 3 位 = 选中槽位 2），group_allows_multi 按选中槽位编址
    "p07_reorder_profile|product|pass|bindings=2,edges=2,multi=2:0:1,avail=IDFXCONSUMEROR:0:2,canonbind=2,fpeq=p04_schema2_or_both"
    "p08_reorder_candidates|product|pass|bindings=2,edges=2,multi=0:0:1,avail=IDFXCONSUMEROR:0:2,canonbind=2,fpeq=p04_schema2_or_both"
    "n01_schema1_ambiguous|parser|AmbiguousProvider|"
    "n02_schema1_missing|parser|MissingProvider|"
    "n03_schema2_and_missing|parser|MissingProvider|"
    "n04_duplicate_candidate|parser|DuplicateCandidate|"
    "n05_self_dependency|parser|SelfDependency|"
    "n06_empty_group|parser|EmptyRequirementGroup|"
    "n07_nested_group|parser|NestedRequirementGroup|"
    "n08_unknown_candidate|parser|UnknownCandidate|"
    "n09_invalid_revision|parser|InvalidRevision|"
    "n10_contract_missing|parser|ContractRequirementMismatch|"
    "n11_contract_extra|parser|ContractCandidateMismatch|"
    "n12_type_mismatch|parser|ManifestTypeMismatch|"
    "c01_cycle_selected|cycle|Cycle|"
    "c02_cycle_unselected|nocycle|pass|"
    "m01_transitive_identity|macros|pass|")

# ---------------------------------------------------------------------------
# 2. 编译器命令（只做语法/语义检查 + 指纹探针的编译链接）
# ---------------------------------------------------------------------------
if(CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -fmessage-length=0：不折行，保证类别/标记片段不会被断成两行（否则计数断言会假失败）
    set(COMPILE_FLAGS -std=c++20 -fsyntax-only -fno-rtti -fno-exceptions -fmessage-length=0 -DID_STATIC_PLUGIN_LINK=1)
    set(INCLUDE_FLAG "-I")
    set(LINK_FLAGS -std=c++20 -O0 -fno-rtti -fno-exceptions -DID_STATIC_PLUGIN_LINK=1)
    set(OUTPUT_FLAG "-o")
    set(COMPILE_SUPPORTED ON)
elseif(CXX_COMPILER_ID STREQUAL "MSVC")
    # /utf-8 与工程一致（源码里有中文注释与中文字面量）：缺了它会按本地代码页 936 解释 UTF-8 源，
    # 直接得到 C4819 + C2001「常量中有换行符」的假失败（实测）。/Zs = 只做语法/语义检查。
    set(COMPILE_FLAGS /nologo /utf-8 /std:c++20 /Zs /GR- /EHs-c- /DID_STATIC_PLUGIN_LINK=1)
    set(INCLUDE_FLAG "/I")
    set(LINK_FLAGS /nologo /utf-8 /std:c++20 /GR- /EHs-c- /DID_STATIC_PLUGIN_LINK=1)
    set(OUTPUT_FLAG "/Fe:")
    set(COMPILE_SUPPORTED ON)
else()
    set(COMPILE_SUPPORTED OFF)
    string(APPEND notes "编译器 ${CXX_COMPILER_ID} 的编译级用例未实现：只跑 configure 期判定（如实标注，不冒充通过）\n")
endif()

# fixture_compile(<out_rc> <log_stem> <source> <include_dirs...>)：只做语法检查
function(fixture_compile out_rc log_stem source)
    set(include_args "")
    foreach(dir IN LISTS ARGN)
        list(APPEND include_args "${INCLUDE_FLAG}${dir}")
    endforeach()
    execute_process(
        COMMAND "${CXX_COMPILER}" ${COMPILE_FLAGS} ${include_args} "${source}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE compile_rc
        OUTPUT_FILE "${log_stem}.out"
        ERROR_FILE  "${log_stem}.err")
    set(${out_rc} "${compile_rc}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# 3. 逐 case 执行
# ---------------------------------------------------------------------------
set(generator_args "")
if(NOT "${GENERATOR}" STREQUAL "")
    set(generator_args -G "${GENERATOR}")
endif()
if(NOT "${GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND generator_args -A "${GENERATOR_PLATFORM}")
endif()
if(NOT "${GENERATOR_INSTANCE}" STREQUAL "")
    list(APPEND generator_args "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
if(NOT "${BUILD_TYPE}" STREQUAL "")
    list(APPEND generator_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

set(case_report "")
set(case_records "")
set(evidence_report "")
set(fingerprints "")
set(passed_cases 0)
set(failed_cases 0)

foreach(entry IN LISTS id_fixture_cases)
    string(REPLACE "|" ";" entry_fields "${entry}")
    list(GET entry_fields 0 case_name)
    list(GET entry_fields 1 case_kind)
    list(GET entry_fields 2 case_expected)
    list(GET entry_fields 3 case_checks)

    set(case_work "${WORK_DIR}/${case_name}")
    set(case_source "${CASE_DIR}/${case_name}")
    if(NOT EXISTS "${case_source}/CMakeLists.txt")
        string(APPEND problems "[${case_name}] fixture 目录缺少 CMakeLists.txt：${case_source}\n")
        math(EXPR failed_cases "${failed_cases} + 1")
        continue()
    endif()

    # 每个 case 从空构建目录开始：避免上一轮缓存掩盖"必须重新解析"的问题
    file(REMOVE_RECURSE "${case_work}")
    file(MAKE_DIRECTORY "${case_work}")

    # ---- 3.1 configure ----
    set(log_stem "${case_work}/configure")
    fixture_run(configure_rc "${log_stem}"
        "${CMAKE_COMMAND}" -S "${case_source}" -B "${case_work}"
        ${generator_args}
        "-DID_FIXTURE_ASSEMBLE_DIR=${ASSEMBLE_DIR}")
    fixture_read_logs(configure_text "${log_stem}")

    set(case_verdict "ok")
    set(case_detail "")

    # 逐 case 的记录字段：报告行与 out/fixture-matrix-cases.txt 共用同一组变量。
    # 缺值一律写 n/a 或 -，不允许留空（留空会让四配置对照表静默漏字段）。
    set(case_configure_rc "${configure_rc}")
    set(case_compile_rc "n/a")
    set(case_compile_state "n/a")
    set(case_compile_marker "none")
    set(case_expected_line "")
    set(case_categories_seen "-")
    set(case_selected "-")
    set(case_bindings "-")
    set(case_edges "-")
    set(case_slots "-")
    set(case_provisions "-")
    set(case_canonbind "-")
    set(case_canonical_sha "-")
    set(case_fingerprint "-")

    if(case_kind STREQUAL "parser")
        # configure 期反例不进入编译级用例：编译退出码显式记作 n/a 而不是留空
        set(case_compile_rc "n/a")
        set(case_compile_state "n/a")
        set(case_expected_line "EXPECTED:${case_expected}")
        string(APPEND case_detail "编译级用例不适用（configure 期已拒绝）；")
        if(configure_rc EQUAL 0)
            set(case_verdict "expected-rejection-but-configured")
            string(APPEND case_detail "；期望 configure 期按 ${case_expected} 拒绝，实际 exit=0")
        else()
            fixture_judge_category(judge_verdict judge_detail "${configure_text}" "${case_expected}")
            set(case_categories_seen "${judge_detail}")
            if(NOT judge_verdict STREQUAL "ok")
                set(case_verdict "${judge_verdict}")
                string(APPEND case_detail "${judge_detail}")
            else()
                string(APPEND case_detail "EXPECTED:${case_expected} ${judge_detail}")
                string(APPEND evidence_report "\n[${case_name}] 期望 ${case_expected}，实际恰好 1 条类别诊断行：\n")
                string(APPEND evidence_report "    ${judge_detail}\n")
            endif()
        endif()
    else()
        # 正例/环/对照/误判：configure 必须先通过
        if(NOT configure_rc EQUAL 0)
            set(case_verdict "configure-failed")
            string(APPEND case_detail "configure exit=${configure_rc}（见 ${log_stem}.out / .err）")
        else()
            # ---- 3.1b 正例形状：EXPECTED:none —— configure 期不允许出现任何类别诊断行 ----
            # （环 c01 的拒绝发生在编译期，所以它的 configure 输出同样必须是"干净"的。）
            fixture_observed_categories(nonparser_categories "${configure_text}")
            list(LENGTH nonparser_categories nonparser_category_count)
            set(case_category_clean TRUE)
            if(nonparser_category_count GREATER 0)
                string(REPLACE ";" " " nonparser_categories_text "${nonparser_categories}")
                set(case_category_clean FALSE)
                set(case_expected_line "EXPECTED:none（实际出现 ${nonparser_categories_text}）")
                set(case_categories_seen "${nonparser_categories_text}")
                string(APPEND case_detail "；正例不应出现类别诊断行，实际 ${nonparser_categories_text}")
            else()
                set(case_expected_line "EXPECTED:none")
                set(case_categories_seen "none")
            endif()

            # ---- 3.2 生成物断言 ----
            fixture_probe_all(count_lines "${configure_text}" "counts")
            list(LENGTH count_lines count_line_count)
            if(NOT count_line_count EQUAL 1)
                set(case_verdict "probe-counts-missing")
                string(APPEND case_detail "探针没有输出唯一一条 counts 行（实际 ${count_line_count} 条）")
            else()
                list(GET count_lines 0 counts_text)
                # counts 行形如：selected=2 bindings=1 edges=1 slots=0 provisions=1
                string(REPLACE " " ";" count_items "${counts_text}")
                set(count_map "")
                foreach(item IN LISTS count_items)
                    if(item MATCHES "^([a-z]+)=([0-9]+)$")
                        set("count_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
                    endif()
                endforeach()
                string(APPEND case_detail "${counts_text}")

                fixture_probe_all(allows_multi "${configure_text}" "allows-multi")
                fixture_probe_all(availability_count "${configure_text}" "availability-count")
                fixture_probe_all(availability_member "${configure_text}" "availability-member")
                fixture_probe_all(canonical_lines_probe "${configure_text}" "canonical")
                fixture_probe_value(canonical_sha "${configure_text}" "canonical-sha256")
                fixture_probe_value(generated_dir "${configure_text}" "generated-dir")
                fixture_probe_value(evidence_file "${configure_text}" "evidence")

                set(canonbind_count 0)
                foreach(canonical_line IN LISTS canonical_lines_probe)
                    if(canonical_line MATCHES "^canonical-binding")
                        math(EXPR canonbind_count "${canonbind_count} + 1")
                    endif()
                endforeach()

                # 记录字段（供报告行与 out/fixture-matrix-cases.txt 使用）
                set(case_selected "${count_selected}")
                set(case_bindings "${count_bindings}")
                set(case_edges "${count_edges}")
                set(case_slots "${count_slots}")
                set(case_provisions "${count_provisions}")
                set(case_canonbind "${canonbind_count}")
                set(case_canonical_sha "${canonical_sha}")

                if(NOT case_checks STREQUAL "")
                    string(REPLACE "," ";" check_items "${case_checks}")
                    foreach(check IN LISTS check_items)
                        if(check MATCHES "^([a-z]+)=(.+)$")
                            set(check_key "${CMAKE_MATCH_1}")
                            set(check_value "${CMAKE_MATCH_2}")
                        else()
                            set(case_verdict "bad-check-spec")
                            string(APPEND case_detail "；无法解析的断言 ${check}")
                            continue()
                        endif()

                        if(check_key STREQUAL "selected" OR check_key STREQUAL "bindings"
                           OR check_key STREQUAL "edges" OR check_key STREQUAL "slots"
                           OR check_key STREQUAL "provisions")
                            if(NOT "${count_${check_key}}" STREQUAL "${check_value}")
                                set(case_verdict "count-mismatch")
                                string(APPEND case_detail "；${check_key} 期望 ${check_value} 实际 ${count_${check_key}}")
                            endif()
                        elseif(check_key STREQUAL "canonbind")
                            if(NOT canonbind_count EQUAL check_value)
                                set(case_verdict "canonical-binding-mismatch")
                                string(APPEND case_detail "；canonical-binding 行数期望 ${check_value} 实际 ${canonbind_count}")
                            endif()
                        elseif(check_key STREQUAL "multi")
                            string(REPLACE ":" ";" multi_fields "${check_value}")
                            list(GET multi_fields 0 multi_slot)
                            list(GET multi_fields 1 multi_group)
                            list(GET multi_fields 2 multi_value)
                            set(wanted "slot=${multi_slot} group=${multi_group} value=1")
                            fixture_in_list(found "${allows_multi}" "${wanted}")
                            if(multi_value STREQUAL "1" AND NOT found)
                                set(case_verdict "allows-multi-missing")
                                string(APPEND case_detail "；期望生成头里有 group_allows_multi[${multi_slot}×32+${multi_group}]=1，实际没有")
                            elseif(multi_value STREQUAL "0" AND found)
                                set(case_verdict "allows-multi-unexpected")
                                string(APPEND case_detail "；生成头里不应有 group_allows_multi[${multi_slot}×32+${multi_group}]，实际存在")
                            endif()
                        elseif(check_key STREQUAL "avail")
                            string(REPLACE ":" ";" avail_fields "${check_value}")
                            list(GET avail_fields 0 avail_token)
                            list(GET avail_fields 1 avail_group)
                            list(GET avail_fields 2 avail_count)
                            set(wanted "token=${avail_token} group=${avail_group} count=${avail_count}")
                            fixture_in_list(found "${availability_count}" "${wanted}")
                            if(NOT found)
                                set(case_verdict "availability-count-mismatch")
                                string(APPEND case_detail "；Matched 头里期望 ${wanted}，实际未找到（已有的：${availability_count}）")
                            endif()
                        elseif(check_key STREQUAL "member" OR check_key STREQUAL "nomember")
                            string(REPLACE ":" ";" member_fields "${check_value}")
                            list(GET member_fields 0 member_token)
                            list(GET member_fields 1 member_group)
                            list(GET member_fields 2 member_provider)
                            set(wanted "token=${member_token} group=${member_group} provider=${member_provider}")
                            fixture_in_list(found "${availability_member}" "${wanted}")
                            if(check_key STREQUAL "member" AND NOT found)
                                set(case_verdict "availability-member-missing")
                                string(APPEND case_detail "；期望 Matched 头里有 ${wanted}，实际没有")
                            elseif(check_key STREQUAL "nomember" AND found)
                                set(case_verdict "availability-member-unexpected")
                                string(APPEND case_detail "；Matched 头里不应出现 ${wanted}，实际存在")
                            endif()
                        elseif(check_key STREQUAL "fpeq")
                            set("fpeq_${case_name}" "${check_value}")
                        else()
                            set(case_verdict "bad-check-spec")
                            string(APPEND case_detail "；未知断言键 ${check_key}")
                        endif()
                    endforeach()
                endif()

                # ---- 3.3 编译级判定 ----
                set(compile_state "not-run")
                if(COMPILE_SUPPORTED)
                    if(case_kind STREQUAL "product")
                        # 校验 TU 必须通过（C++ 静态门禁接受该计划）
                        fixture_compile(validate_rc "${case_work}/compile-validate"
                            "${generated_dir}/IDAssemblyValidate.generated.cpp"
                            "${ID_INCLUDE_DIR}" "${generated_dir}")
                        if(validate_rc EQUAL 0)
                            set(case_detail "${case_detail}；validate=ok")
                        else()
                            set(case_verdict "validate-compile-failed")
                            string(APPEND case_detail "；校验 TU 编译失败 exit=${validate_rc}")
                        endif()

                        # 指纹探针：编译 + 运行，取真实 kProductFingerprint
                        set(probe_exe "${case_work}/fingerprint-probe.exe")
                        set(include_args "${INCLUDE_FLAG}${ID_INCLUDE_DIR}" "${INCLUDE_FLAG}${generated_dir}")
                        execute_process(
                            COMMAND "${CXX_COMPILER}" ${LINK_FLAGS} ${include_args}
                                    "${FIXTURE_SOURCE_DIR}/FixtureFingerprintProbe.cpp" ${OUTPUT_FLAG} "${probe_exe}"
                            WORKING_DIRECTORY "${WORK_DIR}"
                            RESULT_VARIABLE probe_rc
                            OUTPUT_FILE "${case_work}/compile-fingerprint.out"
                            ERROR_FILE  "${case_work}/compile-fingerprint.err")
                        set(fingerprint "-")
                        set(probe_run_rc "n/a")
                        if(NOT probe_rc EQUAL 0)
                            set(case_verdict "fingerprint-probe-compile-failed")
                            string(APPEND case_detail "；指纹探针编译失败 exit=${probe_rc}")
                        else()
                            execute_process(
                                COMMAND "${probe_exe}"
                                WORKING_DIRECTORY "${WORK_DIR}"
                                RESULT_VARIABLE probe_run_rc
                                OUTPUT_VARIABLE fingerprint_out
                                ERROR_VARIABLE  fingerprint_err
                                TIMEOUT 60)
                            if(NOT probe_run_rc EQUAL 0)
                                set(case_verdict "fingerprint-probe-run-failed")
                                string(APPEND case_detail "；指纹探针运行失败 exit=${probe_run_rc}")
                            else()
                                string(REPLACE "\r" "" fingerprint "${fingerprint_out}")
                                string(REPLACE "\n" "" fingerprint "${fingerprint}")
                                if(NOT fingerprint MATCHES "^[0-9]+$")
                                    set(case_verdict "fingerprint-not-numeric")
                                    string(APPEND case_detail "；指纹探针输出不是十进制数：'${fingerprint}'")
                                else()
                                    string(APPEND fingerprints "${case_name}=${fingerprint}\n")
                                    string(APPEND case_detail "；fingerprint=${fingerprint}")
                                endif()
                            endif()
                        endif()
                        set(compile_state "validate-ok fingerprint=${fingerprint}")
                        # 编译退出码逐个子进程留档：报告行与四配置对照都直接引用这些真实退出码
                        set(case_compile_rc "validate:${validate_rc},probe-compile:${probe_rc},probe-run:${probe_run_rc}")
                        if(validate_rc EQUAL 0 AND probe_rc EQUAL 0 AND probe_run_rc EQUAL 0)
                            set(case_compile_state "ok")
                        else()
                            set(case_compile_state "failed")
                        endif()
                        set(case_fingerprint "${fingerprint}")
                    elseif(case_kind STREQUAL "cycle")
                        # 环：校验 TU 必须按 Cycle 拒绝
                        fixture_compile(cycle_rc "${case_work}/compile-cycle"
                            "${generated_dir}/IDAssemblyValidate.generated.cpp"
                            "${ID_INCLUDE_DIR}" "${generated_dir}")
                        fixture_read_logs(cycle_text "${case_work}/compile-cycle")
                        fixture_count_diagnostic_marker(cycle_hits "${cycle_text}" "ID_ASSEMBLY_ERROR:Cycle")
                        fixture_count_diagnostic_marker(cycle_other_hits "${cycle_text}" "ID_ASSEMBLY_ERROR:")
                        if(cycle_rc EQUAL 0)
                            set(case_verdict "cycle-not-rejected")
                            string(APPEND case_detail "；环没有被 C++ 校验拒绝（校验 TU 编译通过）")
                        elseif(NOT cycle_hits EQUAL 1)
                            set(case_verdict "cycle-marker-count")
                            string(APPEND case_detail "；期望恰好 1 处 ID_ASSEMBLY_ERROR:Cycle，实际 ${cycle_hits}")
                        else()
                            string(APPEND case_detail "；EXPECTED:Cycle 标记 1 条（诊断行共 ${cycle_other_hits} 条 ID_ASSEMBLY_ERROR: 断言，其余是计划被拒后必然连带的 FailurePhaseWithoutError / TopologyNotPermutation / EmptyFingerprint）")
                            string(APPEND evidence_report "\n[${case_name}] 编译期拒绝：\n")
                            string(APPEND evidence_report "    ID_ASSEMBLY_ERROR:Cycle 诊断行 ${cycle_hits} 条（期望恰好 1 条）\n")
                            string(APPEND evidence_report "    ID_ASSEMBLY_ERROR: 前缀诊断行共 ${cycle_other_hits} 条（含环被发现后必然连带的 FailurePhaseWithoutError / TopologyNotPermutation / EmptyFingerprint）\n")
                        endif()
                        set(compile_state "cycle-rejected hits=${cycle_hits}")
                        set(case_compile_rc "validate:${cycle_rc}")
                        set(case_compile_state "rejected")
                        set(case_compile_marker "Cycle=${cycle_hits}")
                        set(case_expected_line "EXPECTED:Cycle（编译期拒绝；configure 期无类别诊断行）")
                    elseif(case_kind STREQUAL "nocycle")
                        fixture_compile(nocycle_rc "${case_work}/compile-nocycle"
                            "${generated_dir}/IDAssemblyValidate.generated.cpp"
                            "${ID_INCLUDE_DIR}" "${generated_dir}")
                        if(nocycle_rc EQUAL 0)
                            string(APPEND case_detail "；validate=ok（环候选未选中 -> 不判环）")
                        else()
                            set(case_verdict "nocycle-rejected")
                            string(APPEND case_detail "；未选中环候选却被拒绝 exit=${nocycle_rc}")
                        endif()
                        set(compile_state "validate-ok")
                        set(case_compile_rc "validate:${nocycle_rc}")
                        if(nocycle_rc EQUAL 0)
                            set(case_compile_state "ok")
                        else()
                            set(case_compile_state "failed")
                        endif()
                    elseif(case_kind STREQUAL "macros")
                        # 传递闭包：从探针汇报的真实 PUBLIC 边上算 consumer 能看到哪些身份宏
                        fixture_probe_all(wiring_lines "${configure_text}" "wiring")
                        fixture_probe_all(selected_lines "${configure_text}" "selected")
                        foreach(selected_line IN LISTS selected_lines)
                            if(selected_line MATCHES "target=([^ ]+) macro=([^ ]+)")
                                set("macro_of_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
                            endif()
                        endforeach()
                        # 闭包从 m01 的 consumer target 开始
                        set(closure "IDFxConsumerFp")
                        set(growing TRUE)
                        while(growing)
                            set(growing FALSE)
                            foreach(wiring_line IN LISTS wiring_lines)
                                if(wiring_line MATCHES "dependent=([^ ]+) provider=([^ ]+)")
                                    set(wiring_dependent "${CMAKE_MATCH_1}")
                                    set(wiring_provider "${CMAKE_MATCH_2}")
                                    fixture_in_list(has_dependent "${closure}" "${wiring_dependent}")
                                    if(has_dependent)
                                        fixture_in_list(has_provider "${closure}" "${wiring_provider}")
                                        if(NOT has_provider)
                                            list(APPEND closure "${wiring_provider}")
                                            set(growing TRUE)
                                        endif()
                                    endif()
                                endif()
                            endforeach()
                        endwhile()
                        set(identity_args "")
                        foreach(closure_target IN LISTS closure)
                            if(closure_target STREQUAL "IDFxConsumerFp")
                                continue()
                            endif()
                            if(DEFINED macro_of_${closure_target})
                                list(APPEND identity_args "-D${macro_of_${closure_target}}=1")
                            endif()
                        endforeach()
                        string(REPLACE ";" " " identity_args_text "${identity_args}")

                        # 两个 TU 用同一套编译参数（含同一组身份宏），只有断言方向不同：
                        # 正向对照必须通过，负向用例必须失败 —— 否则"合法对照"不成立。
                        set(include_args "${INCLUDE_FLAG}${ID_INCLUDE_DIR}" "${INCLUDE_FLAG}${generated_dir}")

                        # 正向对照：必须通过（同时证明陷阱真实存在）
                        execute_process(
                            COMMAND "${CXX_COMPILER}" ${COMPILE_FLAGS} ${identity_args} ${include_args}
                                    "${FIXTURE_SOURCE_DIR}/FixtureMatchedSetControl.cpp"
                            WORKING_DIRECTORY "${WORK_DIR}"
                            RESULT_VARIABLE control_rc
                            OUTPUT_FILE "${case_work}/compile-matchedset-control.out"
                            ERROR_FILE  "${case_work}/compile-matchedset-control.err")
                        if(NOT control_rc EQUAL 0)
                            set(case_verdict "matchedset-control-failed")
                            string(APPEND case_detail "；正向对照 TU 编译失败 exit=${control_rc}")
                        endif()

                        # 负向用例：必须失败，且恰好 1 条 EXPECTED:MatchedSetForeignMember
                        set(marker "EXPECTED:MatchedSetForeignMember")
                        execute_process(
                            COMMAND "${CXX_COMPILER}" ${COMPILE_FLAGS} ${identity_args} ${include_args}
                                    "${FIXTURE_SOURCE_DIR}/FixtureMatchedSetForeignMember.cpp"
                            WORKING_DIRECTORY "${WORK_DIR}"
                            RESULT_VARIABLE negative_rc
                            OUTPUT_FILE "${case_work}/compile-matchedset-negative.out"
                            ERROR_FILE  "${case_work}/compile-matchedset-negative.err")
                        fixture_read_logs(negative_text "${case_work}/compile-matchedset-negative")
                        fixture_count_diagnostic_marker(negative_hits "${negative_text}" "${marker}")
                        if(negative_rc EQUAL 0)
                            set(case_verdict "false-positive-not-rejected")
                            string(APPEND case_detail "；负向 TU 竟然编译通过：把传递可见的身份宏当成组成员没有被拒绝")
                        elseif(NOT negative_hits EQUAL 1)
                            set(case_verdict "false-positive-marker-count")
                            string(APPEND case_detail "；期望恰好 1 条 ${marker}，实际 ${negative_hits}")
                        else()
                            string(APPEND case_detail "；${marker} ×1（负向用例已拒绝）")
                            string(APPEND evidence_report "\n[${case_name}] 传递可见性闭包（由真实 PUBLIC 边推导）：${closure}\n")
                            string(APPEND evidence_report "    注入的身份宏：${identity_args_text}\n")
                            string(APPEND evidence_report
                                "    ID_USE_PLUGIN_IDFXCORE 可见但 ID_MATCHED_MEMBER_IDFXCONSUMERFP_G0_IDFXCORE 不存在：\n"
                                "    正向对照 TU 通过（Fixtures 的 #if 检查成立），负向 TU 按 ${marker} 拒绝 ×${negative_hits}\n")
                        endif()
                        set(compile_state "control=${control_rc} negative=${negative_rc} marker=${negative_hits}")
                        set(case_compile_rc "control:${control_rc},negative:${negative_rc}")
                        if(control_rc EQUAL 0 AND NOT negative_rc EQUAL 0)
                            set(case_compile_state "ok")
                        else()
                            set(case_compile_state "failed")
                        endif()
                        set(case_compile_marker "MatchedSetForeignMember=${negative_hits}")
                        set(case_expected_line "EXPECTED:MatchedSetForeignMember（正向对照 TU 通过、负向 TU 编译期拒绝）")
                    endif()
                endif()

                # 证据行（正例/环/对照/误判都记录生成物路径与证据文件）
                string(APPEND evidence_report "\n[${case_name}] ${counts_text}\n")
                if(NOT "${evidence_file}" STREQUAL "")
                    string(APPEND evidence_report "    evidence : ${evidence_file}\n")
                endif()
                string(APPEND evidence_report "    canonical-sha256 : ${canonical_sha}\n")
                string(APPEND evidence_report "    canonbind : ${canonbind_count}（= matched 记录）；edges : ${count_edges}\n")
                string(APPEND evidence_report "    compile : ${compile_state}\n")
                # 生成的 availability（编译期 matched-set 宏）与组语义标志：逐条留档
                foreach(count_line IN LISTS availability_count)
                    string(APPEND evidence_report "    availability : ${count_line}\n")
                endforeach()
                foreach(member_line IN LISTS availability_member)
                    string(APPEND evidence_report "    member : ${member_line}\n")
                endforeach()
                foreach(multi_line IN LISTS allows_multi)
                    string(APPEND evidence_report "    allows-multi : ${multi_line}\n")
                endforeach()
                if(EXISTS "${evidence_file}")
                    # 证据行原文摘录（selected / provider / edge / group / slot），逐条留档。
                    # 用 file(READ) + 手工切行而不是 file(STRINGS)：后者在中文行上会在首个非 ASCII
                    # 字节处截断（实测），会把"（需求组 0，能力 …）"这类关键信息吃掉。
                    file(READ "${evidence_file}" evidence_raw)
                    string(REPLACE "\r" "" evidence_raw "${evidence_raw}")
                    string(REPLACE "\n" ";" evidence_lines "${evidence_raw}")
                    foreach(raw_line IN LISTS evidence_lines)
                        if(raw_line MATCHES "^(selected|provider|edge|group|slot) "
                           AND NOT raw_line MATCHES "^(selected|bindings|edges|slots|provisions) +: ")
                            string(APPEND evidence_report "    | ${raw_line}\n")
                        endif()
                    endforeach()
                endif()
                # 类别诊断行的期望落空时，只要没有更具体的原因就记为不通过
                if(NOT case_category_clean AND case_verdict STREQUAL "ok")
                    set(case_verdict "unexpected-category-diagnostic")
                endif()
            endif()
        endif()
    endif()

    if(case_verdict STREQUAL "ok")
        math(EXPR passed_cases "${passed_cases} + 1")
    else()
        math(EXPR failed_cases "${failed_cases} + 1")
        string(APPEND problems "[${case_name}] ${case_verdict}：${case_detail}\n")
    endif()
    # 报告行固定字段：kind / expect / expected（EXPECTED:<类别> 或 EXPECTED:none）/
    # configure 退出码 / 编译退出码 / 判定结果；其余为证据摘要
    string(APPEND case_report
        "case ${case_name} | kind=${case_kind} | expect=${case_expected} | expected=${case_expected_line} | configure=${case_configure_rc} | compile_exit=${case_compile_rc} | verdict=${case_verdict} | ${case_detail}\n")
    # 机器可读记录：供 cmake/CompareFixtureMatrixRuns.cmake 做四配置逐字段对照（同一行内不许出现 '|'）
    string(APPEND case_records
        "${case_name}|${case_kind}|${case_expected}|${case_configure_rc}|${case_compile_rc}|${case_compile_state}|${case_compile_marker}|${case_verdict}|${case_selected}|${case_bindings}|${case_edges}|${case_slots}|${case_provisions}|${case_canonbind}|${case_canonical_sha}|${case_fingerprint}|${case_categories_seen}\n")
    message(STATUS "fixture-matrix[${case_name}] kind=${case_kind} configure=${case_configure_rc} compile=${case_compile_state} verdict=${case_verdict}")
endforeach()

# ---------------------------------------------------------------------------
# 4. 等价重排：指纹必须一致
# ---------------------------------------------------------------------------
set(reorder_report "")
# 逐个 fpeq 断言在 case 循环里已记录期望，这里统一比对（不依赖 case 表的遍历顺序）
string(REPLACE "\n" ";" fingerprint_lines "${fingerprints}")
foreach(line IN LISTS fingerprint_lines)
    if(NOT line MATCHES "^([^=]+)=([0-9]+)$")
        continue()
    endif()
    set(case_of_line "${CMAKE_MATCH_1}")
    set(value_of_line "${CMAKE_MATCH_2}")
    set(target_case "${fpeq_${case_of_line}}")
    if("${target_case}" STREQUAL "")
        continue()
    endif()
    # 对照基准的指纹（同一份 fingerprints 列表里）
    set(reference_value "")
    foreach(other IN LISTS fingerprint_lines)
        if(other MATCHES "^${target_case}=([0-9]+)$")
            set(reference_value "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if("${reference_value}" STREQUAL "")
        string(APPEND problems "等价重排：${case_of_line} 的对照基准 ${target_case} 没有指纹")
        string(APPEND reorder_report "${case_of_line}: 对照基准 ${target_case} 缺失指纹\n")
    elseif(NOT reference_value STREQUAL value_of_line)
        string(APPEND problems "等价重排指纹不一致：${case_of_line}=${value_of_line} vs ${target_case}=${reference_value}\n")
        string(APPEND reorder_report "${case_of_line}: ${value_of_line} != ${target_case}:${reference_value} MISMATCH\n")
    else()
        string(APPEND reorder_report "${case_of_line}: ${value_of_line} == ${target_case}:${reference_value} same\n")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# 5. 报告、机器可读记录与原始日志（全部只落本构建目录）
# ---------------------------------------------------------------------------
string(TIMESTAMP run_started "%Y-%m-%d %H:%M:%S")
set(report_path   "${REPORT_DIR}/fixture-matrix-report.txt")
set(cases_path    "${REPORT_DIR}/fixture-matrix-cases.txt")
set(meta_path     "${REPORT_DIR}/fixture-matrix-run-meta.txt")

set(case_total 0)
list(LENGTH id_fixture_cases case_total)

# 逐 kind 计数：让"23 个 case 由哪些形状组成"在报告头一眼可见
foreach(kind_name product parser cycle nocycle macros)
    set(kind_count_${kind_name} 0)
endforeach()
string(REPLACE "\n" ";" record_lines_for_kinds "${case_records}")
foreach(record_line IN LISTS record_lines_for_kinds)
    if(record_line STREQUAL "")
        continue()
    endif()
    string(REPLACE "|" ";" record_fields "${record_line}")
    list(GET record_fields 1 record_kind)
    if(DEFINED kind_count_${record_kind})
        math(EXPR kind_count_${record_kind} "${kind_count_${record_kind}} + 1")
    endif()
endforeach()

# 驱动器按 problems 自定的退出码：外部观察值由 RunFixtureMatrixEntry.cmake 回填到报告头
if(problems)
    set(driver_verdict_exit 1)
else()
    set(driver_verdict_exit 0)
endif()

# 报告头字段不许留空（缺数据写 n/a 或说明），否则"配置身份"这一行会静默变空
set(platform_text "${GENERATOR_PLATFORM}")
if("${platform_text}" STREQUAL "")
    set(platform_text "n/a（单配置生成器无平台字段）")
endif()
set(instance_text "${GENERATOR_INSTANCE}")
if("${instance_text}" STREQUAL "")
    set(instance_text "n/a（未指定生成器实例）")
endif()
set(build_type_text "${BUILD_TYPE}")
if("${build_type_text}" STREQUAL "")
    set(build_type_text "n/a（多配置生成器按 config 区分）")
endif()

# 机器可读记录（四配置对照的数据源）。字段顺序固定，消费方按位置取值：
# name|kind|expect|configure_rc|compile_rc|compile_state|compile_marker|verdict|selected|
# bindings|edges|slots|provisions|canonbind|canonical_sha|fingerprint|categories_seen
file(WRITE "${cases_path}"
    "# 组装 fixture 矩阵逐 case 机器可读记录（RunFixtureMatrix.cmake 自动写入，字段顺序固定）\n"
    "# case|kind|expect|configure_rc|compile_rc|compile_state|compile_marker|verdict|selected|bindings|edges|slots|provisions|canonbind|canonical_sha|fingerprint|categories_seen\n"
    "${case_records}")

file(WRITE "${meta_path}"
    "report_version=${ID_FIXTURE_REPORT_VERSION}\n"
    "generated=${run_started}\n"
    "engine_source=${ENGINE_SOURCE_DIR}\n"
    "generator=${GENERATOR}\n"
    "generator_platform=${GENERATOR_PLATFORM}\n"
    "generator_instance=${GENERATOR_INSTANCE}\n"
    "build_type=${BUILD_TYPE}\n"
    "config=${RUN_CONFIG}\n"
    "cxx_id=${CXX_COMPILER_ID}\n"
    "cxx=${CXX_COMPILER}\n"
    "cases=${case_total}\n"
    "passed=${passed_cases}\n"
    "failed=${failed_cases}\n"
    "driver_verdict_exit=${driver_verdict_exit}\n"
    "case_records=${cases_path}\n")

string(CONCAT report_header
    "=== Benchmark/validation/assembly — 组装 fixture 矩阵（configure 期 + 编译期静态门禁）===\n"
    "report version   : ${ID_FIXTURE_REPORT_VERSION}（逐 case 记 configure 退出码 / 编译退出码 / 判定结果 / EXPECTED 行；含未覆盖清单）\n"
    "generated        : ${run_started}\n"
    "scope            : 正确性/结构校验资产；不做性能测量、不产生评级、不写官方 report.txt\n"
    "driver           : ${ASSEMBLY_DIR}/cmake/RunFixtureMatrix.cmake（入口包装 RunFixtureMatrixEntry.cmake 回填外部退出码）\n"
    "engine source    : ${ENGINE_SOURCE_DIR}\n"
    "assemble modules : ${ASSEMBLE_DIR}/cmake/{IDReadCatalog,IDResolveSelection,IDGenerateAssembly}.cmake（include() 复用，未复制）\n"
    "work dir         : ${WORK_DIR}\n"
    "report           : ${report_path}\n"
    "machine records  : ${cases_path}\n"
    "run meta         : ${meta_path}\n"
    "raw log dir      : ${RAW_LOG_DIR}\n"
    "generator        : ${GENERATOR}\n"
    "generator plat   : ${platform_text}\n"
    "generator inst   : ${instance_text}\n"
    "build type       : ${build_type_text}\n"
    "config           : ${RUN_CONFIG}\n"
    "cxx              : ${CXX_COMPILER_ID} @ ${CXX_COMPILER}\n"
    "cases            : ${case_total}（product ${kind_count_product} / parser ${kind_count_parser} / cycle ${kind_count_cycle} / nocycle ${kind_count_nocycle} / macros ${kind_count_macros}）\n"
    "driver exit code : PENDING_EXTERNAL_OBSERVATION\n"
    "driver verdict   : exit=${driver_verdict_exit}（驱动器按 problems 自定；与上一行回填值一致才算自洽）\n"
    "--------------------------------------------------------------------\n"
    "[0] 判定器自检（合成输出：正确必须接受、错误必须拒绝）\n"
    "${selfcheck_report}"
    "--------------------------------------------------------------------\n"
    "[1] 逐 case 结果（expected / configure 退出码 / 编译退出码 / 判定结果 / 证据摘要）\n"
    "${case_report}"
    "--------------------------------------------------------------------\n"
    "[2] 正例 / 对照 证据（生成物计数、canonical 摘要、证据行原文摘录）\n"
    "${evidence_report}\n"
    "--------------------------------------------------------------------\n"
    "[3] 等价重排指纹对照（真实编译期常量 kProductFingerprint）\n"
    "${fingerprints}"
    "${reorder_report}\n"
    "--------------------------------------------------------------------\n"
    "[4] 未覆盖清单（本资产当前形状没有覆盖的判定类别 + 应覆盖方）\n"
    "${ID_FIXTURE_UNCOVERED_TEXT}"
    "--------------------------------------------------------------------\n"
    "[5] 未验证 / 局限\n"
    "  - 本资产不创建插件 target、不产出二进制、不跑运行期生命周期；target 创建、真实 PUBLIC\n"
    "    链接边、flags.make 里的身份宏、产物集合与 Sandbox 运行由既有 IDAssemblyValidationRun 覆盖。\n"
    "  - 编译用例注入的 -DID_USE_PLUGIN_<TOKEN>=1 来自探针汇报的真实 PUBLIC 边传递闭包\n"
    "    （本资产不经过 CMake usage requirement），因此它证明的是「宏语义与 matched 集合的区分」，\n"
    "    不证明 CMake 传播本身；传播由 P2 批次核对 flags.make。\n"
    "  - 环检测属 C++ 静态门控（configure 期解析器不做拓扑排序），因此 c01 的拒绝发生在编译阶段。\n"
    "  - 指纹比较只在同一产品语义的等价重排之间成立，不跨清单变更比较。\n"
    "  - MSVC：cl.exe 不在 PATH，编译级用例要求从 vcvars 环境运行本驱动器\n"
    "    （vcvarsall.bat x64 && cmake --build ...）；外部环境缺 INCLUDE/LIB 时的失败属环境问题，不是判定结论。\n"
    "  - 报告 / 机器可读记录 / 原始日志只写本构建目录（<build>/Benchmark/validation/assembly/out/）；\n"
    "    validation/baseline/raw/** 下的历史目录只作既往证据保留，本资产运行期不再新增写入。\n"
    "    该路径口径与 skills/benchmark/SKILL.md §4.1 及硬约束 13 的旧表述不一致，属本次授权变更；\n"
    "    本资产不改 skill，需主 Agent 确认后由获授权维护者同步。\n"
    "  - 同目录下的 P2 批次目标（IDAssemblyValidationRun）本轮**未变更、未运行**：它的原始日志仍按\n"
    "    skill §4.1 派生到 validation/baseline/raw/assembly-<构建目录名>/，本轮因此没有对该目录树\n"
    "    产生新写入；若要求它同样改为构建目录内写入，属公共口径变更（需同时改 skill），另行授权处理。\n"
    "  - 单份报告只覆盖本配置：四配置（MinGW/MSVC × Debug/Release）一致性不靠本报告声明，\n"
    "    必须用 [6] 的对照工具对四份 machine records 逐字段比对。\n"
    "${notes}"
    "--------------------------------------------------------------------\n"
    "[6] 四配置一致性对照的数据入口\n"
    "  本报告只覆盖「${GENERATOR} / config=${RUN_CONFIG}」这一格。四份配置记录汇总由\n"
    "  ${ASSEMBLY_DIR}/cmake/CompareFixtureMatrixRuns.cmake 生成 out/fixture-matrix-four-config.txt；\n"
    "  对照字段：逐 case 的 expect / configure 退出码 / 编译状态 / 判定结果 / selected / bindings / edges /\n"
    "  provisions / canonical-sha256 / fingerprint / 类别（configure 期类别 + 编译期标记）。\n"
    "  任何不一致都必须在该对照文件里逐条解释，不得用「另一配置通过」互相顶替。\n"
    "--------------------------------------------------------------------\n"
    "SUMMARY\n"
    "  report version : ${ID_FIXTURE_REPORT_VERSION}\n"
    "  cases          : ${case_total}\n"
    "  passed         : ${passed_cases}\n"
    "  failed         : ${failed_cases}\n"
    "  driver verdict : exit=${driver_verdict_exit}\n"
    "  problems       : ${problems}\n"
    "  exit code      : ${driver_verdict_exit}（驱动器判定；外部观察值见报告头 driver exit code 行）\n")

file(WRITE "${report_path}" "${report_header}")

# 原始日志索引：落本构建目录（不再写 validation/baseline/raw/**）
file(WRITE "${RAW_LOG_DIR}/fixture-matrix-index.txt"
    "# 组装 fixture 矩阵批次索引（RunFixtureMatrix.cmake 自动写入；本目录在构建目录内）\n"
    "# 判定：判定器自检按预期拒绝错误输出；configure 期反例恰好 1 条类别诊断行且类别相符；\n"
    "#       正例 configure 期 0 条类别诊断行（EXPECTED:none）；正例生成物计数/availability/\n"
    "#       allows_multi 逐条相符；环按 Cycle 拒绝且对照不拒；传递身份宏误判有正向对照 +\n"
    "#       负向编译用例；等价重排指纹一致。\n"
    "GENERATED     : ${run_started}\n"
    "REPORT_VERSION: ${ID_FIXTURE_REPORT_VERSION}\n"
    "WORK_DIR      : ${WORK_DIR}\n"
    "REPORT        : ${report_path}\n"
    "CASES_FILE    : ${cases_path}\n"
    "META_FILE     : ${meta_path}\n"
    "GENERATOR     : ${GENERATOR} ${GENERATOR_PLATFORM} ${GENERATOR_INSTANCE}\n"
    "BUILD_TYPE    : ${BUILD_TYPE}\n"
    "CONFIG        : ${RUN_CONFIG}\n"
    "CXX           : ${CXX_COMPILER}\n"
    "CXX_ID        : ${CXX_COMPILER_ID}\n"
    "CASES         : ${case_total}\n"
    "PASSED        : ${passed_cases}\n"
    "FAILED        : ${failed_cases}\n"
    "DRIVER_EXIT   : ${driver_verdict_exit}\n"
    "FINGERPRINTS  : ${fingerprints}"
    "REORDER       : ${reorder_report}\n"
    "SELFCHECK     : ${selfcheck_report}\n"
    "PROBLEMS      : ${problems}\n")

message(STATUS "fixture-matrix: cases=${case_total} passed=${passed_cases} failed=${failed_cases}")
message(STATUS "fixture-matrix: report=${report_path}")
message(STATUS "fixture-matrix: machine records=${cases_path}")
message(STATUS "fixture-matrix: run meta=${meta_path}")
message(STATUS "fixture-matrix: raw log dir=${RAW_LOG_DIR}")
message(STATUS "fixture-matrix: driver verdict exit=${driver_verdict_exit}（外部观察值由入口包装回填）")

if(problems)
    message(STATUS "fixture-matrix 问题：\n${problems}")
    message(FATAL_ERROR "组装 fixture 矩阵未通过（详见 ${report_path}）")
endif()
message(STATUS
    "组装 fixture 矩阵通过：判定器自检按预期拒绝错误输出；configure 期反例按具体类别断言；"
    "正例 configure 期无类别诊断行；正例生成物断言相符；环按 Cycle 拒绝且未选中的对照不拒；"
    "传递身份宏误判由正/负编译用例锁定；等价重排指纹一致"
    "（本资产不做性能评级、不写官方 report.txt、运行期只写本构建目录）")
