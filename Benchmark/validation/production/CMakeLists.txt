cmake_minimum_required(VERSION 3.20)

# =============================================================================
# P1 生产 API 正确性与最大规模验证（Benchmark 校验资产，不是生产目标）
#   - 只在显式开启 ID_BUILD_VALIDATION_PRODUCTION 时进入构建；
#   - 只用生产头 Kernel/Plugin.hpp 与 ID::PluginManager 的三个 consteval 操作族，
#     不使用 P0 的 ID::Bench::Fixture 替身 API；
#   - 反向编译用例全部 EXCLUDE_FROM_ALL，不进入普通 Benchmark 的 src/*.cpp glob；
#   - 产物与原始日志按本构建目录隔离：out/{bin,lib} 与
#     Benchmark/validation/baseline/raw/production-<构建目录名>/；
#   - 合法对照（IDProductionLegalCases + ProductionCaseControls）必须先通过，
#     否则驱动器拒绝把任何期望失败用例算通过。
# =============================================================================
if(NOT ID_BUILD_VALIDATION_PRODUCTION)
    return()
endif()

set(IDP_DIR         ${CMAKE_CURRENT_SOURCE_DIR})
set(IDP_INCLUDE_DIR ${IDP_DIR}/include)
set(IDP_OUT_BIN     ${CMAKE_CURRENT_BINARY_DIR}/out/bin)
set(IDP_OUT_LIB     ${CMAKE_CURRENT_BINARY_DIR}/out/lib)

file(MAKE_DIRECTORY ${IDP_OUT_BIN} ${IDP_OUT_LIB})

function(idp_isolate_outputs TARGET)
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${IDP_OUT_BIN}"
        LIBRARY_OUTPUT_DIRECTORY "${IDP_OUT_LIB}"
        ARCHIVE_OUTPUT_DIRECTORY "${IDP_OUT_LIB}")
endfunction()

# SHARED 模式下把内核 DLL 复制到**可执行文件实际所在目录**，控制程序才能独立运行。
# 只写本构建目录，不覆盖共享 bin 里的用户产物（K6：OFF/ON 共用 bin 的既有问题不在本轮修）。
# 目标目录必须用 $<TARGET_FILE_DIR:...> 而不是 IDP_OUT_BIN：多配置生成器（Visual Studio）会给
# RUNTIME_OUTPUT_DIRECTORY 追加 /<CONFIG>，exe 落在 out/bin/Release，直接写 IDP_OUT_BIN 会把 DLL
# 复制到 out/bin（实测：MSVC 构建后 ProductionCaseControls.exe 运行返回 0xC0000135 找不到 ID.dll）。
function(idp_copy_kernel_runtime TARGET)
    if(NOT ID_MONOLITHIC_BUILD)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:ID>" "$<TARGET_FILE_DIR:${TARGET}>/$<TARGET_FILE_NAME:ID>"
            COMMENT "复制 ID 内核 DLL 到 production 可执行文件目录（来源 $<TARGET_FILE:ID>）")
    endif()
endfunction()

# 原始日志目录按构建目录名派生，production 与 P0 prototype、SHARED/STATIC 都不互相覆盖
get_filename_component(IDP_BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
string(REGEX REPLACE "^cmake-build-" "" IDP_RAW_LOG_MODE "${IDP_BUILD_DIR_NAME}")
if(IDP_RAW_LOG_MODE STREQUAL "")
    set(IDP_RAW_LOG_MODE "unnamed-build")
endif()
set(IDP_RAW_LOG_DIR ${IDP_DIR}/../baseline/raw/production-${IDP_RAW_LOG_MODE})
file(MAKE_DIRECTORY ${IDP_RAW_LOG_DIR})
message(STATUS "production 反向用例原始日志目录：${IDP_RAW_LOG_DIR}（构建目录 ${CMAKE_BINARY_DIR}）")

# ---- 1. 用例清单：合法对照 + 每个期望失败目标 -----------------------------
set(IDP_CASE_NAMES
    active_overflow
    ambiguous_old_new
    ambiguous_provider
    candidate_capability_mismatch
    catalog_overflow
    contract_candidate_mismatch
    contract_count_mismatch
    cycle
    cycle_tail
    duplicate_active
    duplicate_candidate
    duplicate_capability_declaration
    duplicate_capability_slots
    duplicate_capability_unused
    duplicate_plugin
    empty_inner_group
    evidence_binding_index_bounds
    evidence_capacity_binding
    evidence_capacity_edge
    evidence_capacity_runtime
    evidence_capacity_selection
    evidence_capacity_slot
    evidence_duplicate_binding
    evidence_duplicate_edge
    evidence_duplicate_runtime
    evidence_duplicate_selection
    evidence_duplicate_slot
    evidence_edge_mismatch
    evidence_identity_mismatch
    evidence_macro_mismatch
    evidence_provider_mismatch
    evidence_selection_mismatch
    evidence_slot_index_bounds
    evidence_slot_mismatch
    evidence_target_mismatch
    hash_collision
    interface_lifecycle_mismatch
    interface_slot_mismatch
    invalid_active
    invalid_capability
    invalid_namespace
    invalid_revision
    invalid_revision_range
    invalid_slot
    invalid_target
    missing_provider
    namespace_conflict
    name_boundaries
    name_first_char
    name_internal_nul
    name_unterminated
    plugin_version_not_api
    pod_candidate_count_capacity
    pod_contract_count_capacity
    pod_group_count_capacity
    pod_invalid_kind
    pod_invalid_lifecycle
    pod_pool_bounds
    revision_mismatch
    runtime_binding_id_mismatch
    runtime_binding_revision_mismatch
    schema_mismatch
    self_dependency
    slot_conflict
    target_macro_conflict
    too_many_candidates
    too_many_provides
    too_many_requirements
    unknown_active
    unknown_candidate)

set(IDP_CASE_SOURCES "")
foreach(case_name IN LISTS IDP_CASE_NAMES)
    list(APPEND IDP_CASE_SOURCES ${IDP_DIR}/src/cases/case_${case_name}.cpp)
endforeach()

# 合法对照：同一批 TU，加 PRODUCTION_CASE_LEGAL 编译定义
add_library(IDProductionLegalCases OBJECT ${IDP_CASE_SOURCES})
target_compile_definitions(IDProductionLegalCases PRIVATE PRODUCTION_CASE_LEGAL)
target_include_directories(IDProductionLegalCases PRIVATE ${IDP_INCLUDE_DIR})
target_link_libraries(IDProductionLegalCases PRIVATE ID)

# ---- 正向对照：6 个 TU 必须显式注册成编译目标 ------------------------------
# 显式清单 + configure 期"磁盘文件必须已注册"检查：新增正向 TU 却忘记登记时直接报错，
# 不再出现"文件存在但没有被任何目标编译、却被证据宣称已验证"的情况。
set(IDP_POSITIVE_NAMES
    positive_basics
    positive_graph
    positive_ownership
    positive_permutation
    positive_storage
    positive_uneven)

set(IDP_POSITIVE_FILES "")
foreach(positive_name IN LISTS IDP_POSITIVE_NAMES)
    list(APPEND IDP_POSITIVE_FILES ${IDP_DIR}/src/positive/${positive_name}.cpp)
endforeach()

file(GLOB IDP_POSITIVE_ON_DISK CONFIGURE_DEPENDS ${IDP_DIR}/src/positive/positive_*.cpp)
foreach(positive_file IN LISTS IDP_POSITIVE_ON_DISK)
    get_filename_component(positive_name "${positive_file}" NAME_WE)
    list(FIND IDP_POSITIVE_NAMES "${positive_name}" positive_registered)
    if(positive_registered EQUAL -1)
        message(FATAL_ERROR
            "positive 源文件未注册：${positive_name}.cpp 存在于 src/positive 但没有进入 IDP_POSITIVE_NAMES，"
            "任何证据都不得声称它已被编译。")
    endif()
endforeach()
list(LENGTH IDP_POSITIVE_NAMES IDP_POSITIVE_REGISTERED_COUNT)
list(LENGTH IDP_POSITIVE_ON_DISK IDP_POSITIVE_DISK_COUNT)
if(NOT IDP_POSITIVE_REGISTERED_COUNT EQUAL IDP_POSITIVE_DISK_COUNT)
    message(FATAL_ERROR
        "positive 注册数量与磁盘数量不一致：注册 ${IDP_POSITIVE_REGISTERED_COUNT} / 磁盘 ${IDP_POSITIVE_DISK_COUNT}")
endif()

add_library(IDProductionPositiveCases OBJECT ${IDP_POSITIVE_FILES})
target_include_directories(IDProductionPositiveCases PRIVATE ${IDP_INCLUDE_DIR})
target_link_libraries(IDProductionPositiveCases PRIVATE ID)

# MSVC 静态 CRT + 源编码：两个 OBJECT 库与 ProductionCaseControls（走 id_apply_performance_flags）
# 必须是同一套 C++ 运行时，否则 /MD 的 .obj 链进 /MT 的 exe 会触发
# LNK2038（RuntimeLibrary 不匹配：MT_StaticRelease vs MD_DynamicRelease）；
# 同时这两个库里的用例 TU 含中文静态断言消息，缺 /utf-8 时 MSVC 会按系统 ANSI 解析报 C2001。
# 只补这两条最小属性，不引入优化参数、不改任何用例语义。
if(MSVC)
    foreach(idp_crt_target IN ITEMS IDProductionLegalCases IDProductionPositiveCases)
        set_property(TARGET ${idp_crt_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(${idp_crt_target} PRIVATE /utf-8)
    endforeach()
endif()

# 合法对照可执行文件：全部反向用例的合法分支 OBJECT + 6 个正向 OBJECT 真实进入链接行；
# 6 个正向 TU 各导出一个探针符号，main 逐个调用并核对读数（64 个合法分支没有可调用符号，
# 其校验是编译期 static_assert，因此不声称运行期调用过它们）。
add_executable(ProductionCaseControls
    ${IDP_DIR}/src/cases/ProductionCaseControl.cpp
    $<TARGET_OBJECTS:IDProductionLegalCases>
    $<TARGET_OBJECTS:IDProductionPositiveCases>)
target_include_directories(ProductionCaseControls PRIVATE ${IDP_INCLUDE_DIR})
target_link_libraries(ProductionCaseControls PRIVATE ID IDPluginStub)
if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(ProductionCaseControls)
endif()
idp_isolate_outputs(ProductionCaseControls)
idp_copy_kernel_runtime(ProductionCaseControls)

# 期望失败：每个 TU 单独编译，EXCLUDE_FROM_ALL
foreach(case_name IN LISTS IDP_CASE_NAMES)
    add_library(production_expect_${case_name} OBJECT EXCLUDE_FROM_ALL
        ${IDP_DIR}/src/cases/case_${case_name}.cpp)
    target_include_directories(production_expect_${case_name} PRIVATE ${IDP_INCLUDE_DIR})
    target_link_libraries(production_expect_${case_name} PRIVATE ID)
endforeach()

# 驱动器自检：故意写错期望类别 / 类别正确但辅助断言失败，必须被驱动器拒绝
add_library(production_expect_wrong_expectation OBJECT EXCLUDE_FROM_ALL
    ${IDP_DIR}/src/cases/case_wrong_expectation.cpp)
target_include_directories(production_expect_wrong_expectation PRIVATE ${IDP_INCLUDE_DIR})
target_link_libraries(production_expect_wrong_expectation PRIVATE ID)

add_library(production_expect_aux_assert_failure OBJECT EXCLUDE_FROM_ALL
    ${IDP_DIR}/src/cases/case_aux_assert_failure.cpp)
target_include_directories(production_expect_aux_assert_failure PRIVATE ${IDP_INCLUDE_DIR})
target_link_libraries(production_expect_aux_assert_failure PRIVATE ID)

# 正向自检：同一份正向输入只把一条断言改成错误期望，编译必须失败。
# 若正向 TU 根本没被编译/断言没被求值，本目标会静默通过，驱动器据此判定自检失败。
add_library(production_positive_mutated OBJECT EXCLUDE_FROM_ALL
    ${IDP_DIR}/src/positive/mutated_assert_probe.cpp)
target_include_directories(production_positive_mutated PRIVATE ${IDP_INCLUDE_DIR})
target_link_libraries(production_positive_mutated PRIVATE ID)

# MSVC 源编码：期望失败目标同样 include 生产头，而这些头里的 static_assert 消息是中文。
# 缺 /utf-8 时 MSVC 按系统 ANSI 代码页解析无 BOM 的 UTF-8 源，先在
# `ID/include/Kernel/PluginCompileTime.hpp` / `Plugin.hpp` 报 C2001（字符串字面量中的换行符），
# 解析失败的 `ID::PluginManager` 随后派生出 C2039（"register_plugin"/"sort_and_validate" 不是成员）。
# 这会让"期望失败"看起来通过、实际失败原因完全不对（驱动器会拒绝，但资产不可用）。
# 与 prototype 的用例目标同一口径：只补项目基线里的 /utf-8，不改任何断言与用例语义。
if(MSVC)
    set(IDP_ENCODING_TARGETS
        production_expect_wrong_expectation
        production_expect_aux_assert_failure
        production_positive_mutated)
    foreach(case_name IN LISTS IDP_CASE_NAMES)
        list(APPEND IDP_ENCODING_TARGETS production_expect_${case_name})
    endforeach()
    foreach(encoding_target IN LISTS IDP_ENCODING_TARGETS)
        target_compile_options(${encoding_target} PRIVATE /utf-8)
    endforeach()
endif()

# 用例|期望类别：驱动器逐条编译并按"退出码非 0 + 断言数恰好 1 + EXPECTED + 无 MISMATCH"判定
set(IDP_NEGATIVE_CASES
    "active_overflow|TooManyActive,ambiguous_old_new|AmbiguousProvider,ambiguous_provider|AmbiguousProvider,candidate_capability_mismatch|CandidateCapabilityMismatch,catalog_overflow|TooManyCatalog,contract_candidate_mismatch|ContractCandidateMismatch,contract_count_mismatch|ContractRequirementMismatch,cycle|Cycle,cycle_tail|Cycle,duplicate_active|DuplicateActive,duplicate_candidate|DuplicateCandidate,duplicate_capability_declaration|DuplicateCapability,duplicate_capability_slots|DuplicateCapability,duplicate_capability_unused|DuplicateCapability,duplicate_plugin|DuplicatePlugin,empty_inner_group|EmptyRequirementGroup,evidence_binding_index_bounds|EvidenceProviderMismatch,evidence_capacity_binding|EvidenceProviderMismatch,evidence_capacity_edge|EvidenceEdgeMismatch,evidence_capacity_runtime|RuntimeBindingMismatch,evidence_capacity_selection|EvidenceSelectionMismatch,evidence_capacity_slot|EvidenceSlotMismatch,evidence_duplicate_binding|EvidenceProviderMismatch,evidence_duplicate_edge|EvidenceEdgeMismatch,evidence_duplicate_runtime|RuntimeBindingMismatch,evidence_duplicate_selection|EvidenceSelectionMismatch,evidence_duplicate_slot|EvidenceSlotMismatch,evidence_edge_mismatch|EvidenceEdgeMismatch,evidence_identity_mismatch|EvidenceIdentityMismatch,evidence_macro_mismatch|EvidenceMacroMismatch,evidence_provider_mismatch|EvidenceProviderMismatch,evidence_selection_mismatch|EvidenceSelectionMismatch,evidence_slot_index_bounds|EvidenceSlotMismatch,evidence_slot_mismatch|EvidenceSlotMismatch,evidence_target_mismatch|EvidenceTargetMismatch,hash_collision|NameHashCollision,interface_lifecycle_mismatch|InterfaceLifecycleMismatch,interface_slot_mismatch|InterfaceLifecycleMismatch,invalid_active|InvalidActive,invalid_capability|InvalidCapability,invalid_namespace|InvalidNamespace,invalid_revision|InvalidRevision,invalid_revision_range|InvalidRevisionRange,invalid_slot|InvalidSlot,invalid_target|InvalidTargetName,missing_provider|MissingProvider,namespace_conflict|NamespaceConflict,name_boundaries|InvalidName,name_first_char|InvalidName,name_internal_nul|InvalidName,name_unterminated|InvalidName,plugin_version_not_api|RevisionMismatch,pod_candidate_count_capacity|InvalidDeclaration,pod_contract_count_capacity|InvalidDeclaration,pod_group_count_capacity|InvalidDeclaration,pod_invalid_kind|InvalidDeclaration,pod_invalid_lifecycle|InvalidDeclaration,pod_pool_bounds|InvalidDeclaration,revision_mismatch|RevisionMismatch,runtime_binding_id_mismatch|RuntimeBindingMismatch,runtime_binding_revision_mismatch|RuntimeBindingMismatch,schema_mismatch|SchemaMismatch,self_dependency|SelfDependency,slot_conflict|SlotConflict,target_macro_conflict|TargetMacroConflict,too_many_candidates|TooManyCandidates,too_many_provides|TooManyProvides,too_many_requirements|TooManyRequirements,unknown_active|UnknownActive,unknown_candidate|UnknownCandidate")

add_custom_target(ProductionNegativeCases
    COMMAND ${CMAKE_COMMAND}
        -DPRODUCTION_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        "-DBATCH_CWD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DMONOLITHIC=${ID_MONOLITHIC_BUILD}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        "-DRAW_LOG_DIR=${IDP_RAW_LOG_DIR}"
        "-DRAW_LOG_MODE=production-${IDP_RAW_LOG_MODE}"
        "-DCASES=${IDP_NEGATIVE_CASES}"
        -DWRONG_EXPECT_CASE=wrong_expectation
        -DAUX_FAIL_CASE=aux_assert_failure
        -DLEGAL_OBJECT_TARGET=IDProductionLegalCases
        -DPOSITIVE_OBJECT_TARGET=IDProductionPositiveCases
        -DCONTROL_TARGET=ProductionCaseControls
        "-DCONTROL_EXE=$<TARGET_FILE:ProductionCaseControls>"
        "-DPOSITIVE_NAMES=${IDP_POSITIVE_NAMES}"
        "-DPOSITIVE_SOURCE_DIR=${IDP_DIR}/src/positive"
        -DFORCE_REBUILD_CONTROLS=ON
        -DMUTATED_ASSERT_TARGET=production_positive_mutated
        -P ${IDP_DIR}/cmake/RunProductionCases.cmake
    VERBATIM
    COMMENT "production negative cases: expect-fail targets + legal/positive control + self-checks")

# ---- 2. 最大规模与常量求值成本档（全部 EXCLUDE_FROM_ALL） -----------------
set(IDP_SCALE_NAMES
    scale_active_128
    scale_32x32x1
    scale_128x32x1
    scale_128x32x2
    scale_128x32x4
    scale_128x32x8
    scale_limit_128x32x16
    scale_plan_only_128x32x1
    scale_plan_only_128x32x16)

foreach(scale_name IN LISTS IDP_SCALE_NAMES)
    add_library(production_${scale_name} OBJECT EXCLUDE_FROM_ALL ${IDP_DIR}/src/scale/${scale_name}.cpp)
    target_include_directories(production_${scale_name} PRIVATE ${IDP_INCLUDE_DIR})
    target_link_libraries(production_${scale_name} PRIVATE ID)
endforeach()

# MSVC 源编码：规模档同样 include 生产头，而这些头里的 static_assert 消息是中文。
# 缺 /utf-8 时 MSVC 按系统 ANSI 代码页解析无 BOM 的 UTF-8 源，先在
# `Kernel/PluginCompileTime.hpp` / `Kernel/Plugin.hpp` 报 C2001（字符串字面量中的换行符），
# 于是"规模档能不能编译"根本无法判定（这是实测：未补该选项时 production_scale_active_128
# 在 MSVC Debug 下就是这个报错，而不是常量求值超限）。
# 与上面 legal/positive/expect 目标同一口径：只补项目基线里的 /utf-8，
# 不改任何用例语义、不改容量、不加优化参数。
if(MSVC)
    foreach(scale_name IN LISTS IDP_SCALE_NAMES)
        target_compile_options(production_${scale_name} PRIVATE /utf-8)
    endforeach()
    # 已知未冻结项（不在本资产内抬预算）：MSVC 默认 /constexpr:steps 下 128 active × 32 需求档
    # 会报 C2131（实测：scale_128x32x1）。探针实测该档在 /constexpr:steps2147483647 下可通过
    # （35 s），说明障碍是默认步数预算而不是用例本身；本资产未对 MSVC 步数做二分，
    # 因此不写"实测最小档"、也不把 MSVC 规模档计入通过结论。
endif()

# A03（kSchemaRevision = 2）把计划结果换成 matched 静态计划表：
#   match_begin[kGroupSlots + 1] = 4097 × 4B + match_active[kMatchSlots] = 65536 × 2B
#   + edge_target[16256] × 2B ≈ 180 KiB / 每个结果对象，
# 128 active 档的常量求值成本因此越过 GCC 默认 -fconstexpr-ops-limit=33554432。
# 实测（GCC 16.2.0，本机，单 TU 直接编译 + 真实目标构建复核）：
#   - 默认 33554432 下 128x32x2 / 128x32x4 / 128x32x8 / 128x32x16 全部
#     "'constexpr' evaluation operation count exceeds limit"；
#   - 128x32x1 与 32x32x1 仍可通过默认预算，因此这两档不加该参数；
#   - 16 候选档二分：134217728 失败、183500800 失败、184549376 失败（A03 之前曾通过）、
#     189792256 失败、192413696 通过 -> 192413696 是本次实测最小通过档；
#   - 2 / 4 / 8 候选档在同一上界下实测通过（未逐档二分，不声称各自的最小档）。
# 上界仍是有限值、容量口径不变；scale_limit_128x32x16 与 scale_plan_only_128x32x16
# 保持默认预算，继续记录"默认限制档"的失败，不抬高也不下调。
set(IDP_SCALE_CONSTEXPR_OPS_LIMIT 192413696)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(budget_scale IN ITEMS scale_128x32x2 scale_128x32x4 scale_128x32x8)
        target_compile_options(production_${budget_scale} PRIVATE
            -fconstexpr-ops-limit=${IDP_SCALE_CONSTEXPR_OPS_LIMIT})
    endforeach()
    message(STATUS "production 规模档 production_scale_128x32x{2,4,8} 使用 -fconstexpr-ops-limit=${IDP_SCALE_CONSTEXPR_OPS_LIMIT}（GCC 限定，A03 表体积导致）")
endif()

# 有限预算压力目标：只对 GNU 目标追加明确的有限上界，普通目标不加（也不进全局 CMAKE_CXX_FLAGS）。
# 取值与上一条规模档共用 IDP_SCALE_CONSTEXPR_OPS_LIMIT：A03 之前该档的最小通过档是 184549376，
# A03 的 matched 静态计划表把成本抬高到 184549376 也超限（实测），本次重新二分得到 192413696。
# 这里同样写实测最小通过档，而不是留大余量；上界仍是有限值，不是"抬高到不失败为止"。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_library(production_scale_target_128x32x16 OBJECT EXCLUDE_FROM_ALL
        ${IDP_DIR}/src/scale/scale_target_128x32x16.cpp)
    target_include_directories(production_scale_target_128x32x16 PRIVATE ${IDP_INCLUDE_DIR})
    target_link_libraries(production_scale_target_128x32x16 PRIVATE ID)
    target_compile_options(production_scale_target_128x32x16 PRIVATE
        -fconstexpr-ops-limit=${IDP_SCALE_CONSTEXPR_OPS_LIMIT})
    message(STATUS "production 压力目标 production_scale_target_128x32x16 使用 -fconstexpr-ops-limit=${IDP_SCALE_CONSTEXPR_OPS_LIMIT}（GCC 限定，实测最小可通过档）")
else()
    message(STATUS "production 压力目标跳过：有限常量求值预算只在 GCC 16.2.0 上实测")
endif()
