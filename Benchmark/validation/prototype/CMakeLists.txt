cmake_minimum_required(VERSION 3.20)

# =============================================================================
# P0 编译期插件声明原型（Benchmark 校验资产，不是生产目标）
#   - 只在显式开启 ID_BUILD_VALIDATION_PROTOTYPE 时进入构建，默认不进入普通 Benchmark 构建；
#   - 通过父工程拿到的既有目标：ID（内核）、IDPluginStub（分配桩）、id_apply_performance_flags；
#   - 反向编译用例全部 EXCLUDE_FROM_ALL，绝不进入普通 Benchmark 可执行文件的源码 glob；
#   - 本子工程的所有产物写到自己的 out/ 目录（各 build 目录独立），
#     不再与根工程共用的 bin/bin 互相覆盖，构建类型/模式可以逐项核对；
#   - 反向用例的原始日志写到自己的 raw/<模式>/ 子目录（同样按 build 目录派生），
#     SHARED / STATIC 互不覆盖（见下面"原始日志"一节）。
# =============================================================================
if(NOT ID_BUILD_VALIDATION_PROTOTYPE)
    return()
endif()

set(IDF_DIR          ${CMAKE_CURRENT_SOURCE_DIR})
set(IDF_INCLUDE_DIR  ${IDF_DIR}/include)
set(IDF_TEMPLATE_DIR ${IDF_DIR}/templates)
set(IDF_GEN_DIR      ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(IDF_OUT_BIN      ${CMAKE_CURRENT_BINARY_DIR}/out/bin)
set(IDF_OUT_LIB      ${CMAKE_CURRENT_BINARY_DIR}/out/lib)

# ---- 原始日志：按构建模式分目录，SHARED / STATIC 同批运行也不会互相覆盖 ----------
# 目录名由构建目录派生（cmake-build-protoshared -> raw/protoshared，
# cmake-build-protostatic -> raw/protostatic），因此 "cmake --build <build dir>
# --target FixtureNegativeCases" 会自动落到该模式自己的目录；
# 逐用例日志与 summary 都按固定名字写在该目录内，不再需要手工改名。
# 目录归属由 RunNegativeCases.cmake 写入的 negative-index.txt 记录，构建目录变化会报错而不是静默复用。
get_filename_component(IDF_BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
string(REGEX REPLACE "^cmake-build-" "" IDF_RAW_LOG_MODE "${IDF_BUILD_DIR_NAME}")
if(IDF_RAW_LOG_MODE STREQUAL "")
    set(IDF_RAW_LOG_MODE "unnamed-build")
endif()
set(IDF_RAW_LOG_ROOT ${IDF_DIR}/../baseline/raw)
set(IDF_RAW_LOG_DIR  ${IDF_RAW_LOG_ROOT}/${IDF_RAW_LOG_MODE})
file(MAKE_DIRECTORY ${IDF_GEN_DIR} ${IDF_OUT_BIN} ${IDF_OUT_LIB} ${IDF_RAW_LOG_DIR})
message(STATUS "反向用例原始日志目录：${IDF_RAW_LOG_DIR}（模式 ${IDF_RAW_LOG_MODE}，构建目录 ${CMAKE_BINARY_DIR}）")

include(${IDF_DIR}/cmake/IDFixtureModule.cmake)
include(${IDF_DIR}/cmake/IDFixtureResolve.cmake)
include(${IDF_DIR}/assembly/catalog.cmake)

# ---- 原型产物隔离：每个 build 目录只写自己的 out/，不碰根工程的 bin ----------------
function(idf_isolate_outputs TARGET)
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${IDF_OUT_BIN}"
        LIBRARY_OUTPUT_DIRECTORY "${IDF_OUT_LIB}"
        ARCHIVE_OUTPUT_DIRECTORY "${IDF_OUT_LIB}")
endfunction()

# SHARED 模式下把内核 DLL 复制到原型自己的可执行文件旁边。
# 来源是根工程构建出来的 $<TARGET_FILE:ID>；只写本子工程的 out/bin，不覆盖用户产物。
# 目标目录必须用 $<TARGET_FILE_DIR:${TARGET}> 而不是 IDF_OUT_BIN：
#   多配置生成器（Visual Studio）给 RUNTIME_OUTPUT_DIRECTORY 自动追加 /<CONFIG>，
#   exe 落在 out/bin/Debug，而直接写 IDF_OUT_BIN 会把 ID.dll 复制到 out/bin，
#   运行时找不到 DLL（实测退出码 -1073741515 / 0xC0000135）。
#   单配置生成器（MinGW Makefiles）下 $<TARGET_FILE_DIR> 仍等于 out/bin，行为不变。
function(idf_copy_kernel_runtime TARGET)
    if(NOT ID_MONOLITHIC_BUILD)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ID>" "$<TARGET_FILE_DIR:${TARGET}>"
            COMMENT "复制 ID 内核 DLL 到原型可执行文件旁边（来源 $<TARGET_FILE:ID>，目标 $<TARGET_FILE_DIR:${TARGET}>）")
    endif()
endfunction()

# ---- 1. 单一元数据源 -> 解析结果 + 生成物 -----------------------------------
id_fixture_resolve(
    CATALOG       ${ID_FIXTURE_CATALOG_MANIFESTS}
    PROFILE       ${IDF_DIR}/assembly/profiles/fixture_default.json
    GENERATED_DIR ${IDF_GEN_DIR}
    HEADER_NAME   FixtureAssembly.generated.hpp
    OUT_PREFIX    IDF_MAIN)

id_fixture_resolve(
    CATALOG       ${ID_FIXTURE_CATALOG_MANIFESTS}
    PROFILE       ${IDF_DIR}/assembly/profiles/fixture_zero.json
    GENERATED_DIR ${IDF_GEN_DIR}
    HEADER_NAME   FixtureZeroAssembly.generated.hpp
    OUT_PREFIX    IDF_ZERO)

message(STATUS "fixture 产品选择：${IDF_MAIN_SELECTED_NAMES}")
message(STATUS "fixture 链接边：${IDF_MAIN_WIRING}")
message(STATUS "fixture 零插件选择数：${IDF_ZERO_SELECTED_NAMES}")

# ---- 2. 先建节点：每个插件只声明自己的源码 --------------------------------
add_subdirectory(${IDF_DIR}/plugins/fixture_alpha)
add_subdirectory(${IDF_DIR}/plugins/fixture_beta)

# ---- 3. 再按解析结果接线：PUBLIC 上游 + PUBLIC 身份宏 ----------------------
id_fixture_apply_wiring(${IDF_MAIN_WIRING})

# ---- 4. 装配静态库：编译期计划 + 绑定表 + 产品入口 ------------------------
add_library(IDFixtureAssembly STATIC
    ${IDF_DIR}/src/assembly/FixturePlan.cpp
    ${IDF_DIR}/src/assembly/FixtureDeterminism.cpp
    ${IDF_DIR}/src/assembly/FixtureProductEntry.cpp)
target_include_directories(IDFixtureAssembly PUBLIC ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
target_link_libraries(IDFixtureAssembly PUBLIC ID)
target_link_libraries(IDFixtureAssembly PUBLIC ${IDF_MAIN_SELECTED_TARGETS})
target_link_libraries(IDFixtureAssembly PRIVATE IDPluginStub)
if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(IDFixtureAssembly)
endif()
idf_isolate_outputs(IDFixtureAssembly)

# ---- 5. 两个模块的产品（STATIC 或 SHARED 由 ID_MONOLITHIC_BUILD 决定） ----
add_executable(FixturePrototype ${IDF_DIR}/src/apps/PrototypeMain.cpp)
target_link_libraries(FixturePrototype PRIVATE IDFixtureAssembly ID IDPluginStub)
if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(FixturePrototype)
endif()
idf_isolate_outputs(FixturePrototype)
idf_copy_kernel_runtime(FixturePrototype)

# ---- 6. 零插件产品：只认识 ID，不链接任何 fixture 模块 --------------------
add_executable(FixtureZeroPrototype ${IDF_DIR}/src/apps/ZeroPluginMain.cpp)
target_include_directories(FixtureZeroPrototype PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
target_link_libraries(FixtureZeroPrototype PRIVATE ID IDPluginStub)
if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(FixtureZeroPrototype)
endif()
idf_isolate_outputs(FixtureZeroPrototype)
idf_copy_kernel_runtime(FixtureZeroPrototype)

# ---- 7. 反向编译用例：合法对照 + 逐个期望失败 -----------------------------
set(IDF_CASE_NAMES
    self_dependency
    ambiguous_provider
    missing_provider
    cycle
    hash_collision
    duplicate_candidate
    empty_inner_group
    unknown_candidate
    active_overflow
    evidence_mismatch
    invalid_name
    duplicate_plugin
    too_many_requirements
    too_many_candidates
    too_many_catalog
    unknown_active
    duplicate_active
    evidence_selection_mismatch
    evidence_edge_mismatch
    evidence_duplicate_selection
    evidence_duplicate_binding
    evidence_duplicate_edge
    evidence_capacity
    self_dependency_prefix_collision
    cycle_long
    name_boundaries
    name_internal_nul
    name_first_char)

set(IDF_CASE_SOURCES "")
foreach(case_name IN LISTS IDF_CASE_NAMES)
    list(APPEND IDF_CASE_SOURCES ${IDF_DIR}/src/cases/case_${case_name}.cpp)
endforeach()

# 合法对照：同一批 TU，加 FIXTURE_CASE_LEGAL 编译定义，编成 OBJECT 后链接进一个可执行文件
add_library(IDFixtureLegalCases OBJECT ${IDF_CASE_SOURCES})
target_compile_definitions(IDFixtureLegalCases PRIVATE FIXTURE_CASE_LEGAL)
target_include_directories(IDFixtureLegalCases PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
target_link_libraries(IDFixtureLegalCases PRIVATE ID)

add_executable(FixtureCaseControls
    ${IDF_DIR}/src/cases/CaseControlMain.cpp
    $<TARGET_OBJECTS:IDFixtureLegalCases>)
target_link_libraries(FixtureCaseControls PRIVATE ID IDPluginStub)
idf_isolate_outputs(FixtureCaseControls)
idf_copy_kernel_runtime(FixtureCaseControls)

# 期望失败：同一个源文件的另一份编译（不带 FIXTURE_CASE_LEGAL），EXCLUDE_FROM_ALL
foreach(case_name IN LISTS IDF_CASE_NAMES)
    add_library(fixture_expect_${case_name} OBJECT EXCLUDE_FROM_ALL
        ${IDF_DIR}/src/cases/case_${case_name}.cpp)
    target_include_directories(fixture_expect_${case_name} PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
    target_link_libraries(fixture_expect_${case_name} PRIVATE ID)
endforeach()

# 驱动器自检目标（两个，都不计入 28 条反向用例）：
#   1. wrong_expectation：输入的错误类别被故意写错，判定必须拒绝（防"只看编译失败"）；
#   2. aux_assert_failure：类别断言正确、但一条辅助断言也失败，判定必须拒绝
#      （防"删掉辅助断言换取通过"，也防把辅助断言的单独失败当成类别正确）。
add_library(fixture_expect_wrong_expectation OBJECT EXCLUDE_FROM_ALL
    ${IDF_DIR}/src/cases/case_wrong_expectation.cpp)
target_include_directories(fixture_expect_wrong_expectation PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
target_link_libraries(fixture_expect_wrong_expectation PRIVATE ID)

add_library(fixture_expect_aux_assert_failure OBJECT EXCLUDE_FROM_ALL
    ${IDF_DIR}/src/cases/case_aux_assert_failure.cpp)
target_include_directories(fixture_expect_aux_assert_failure PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
target_link_libraries(fixture_expect_aux_assert_failure PRIVATE ID)

# 反向用例与自检 TU 的 static_assert 消息里有中文字符串字面量：MSVC 在缺 /utf-8 时按系统
# ANSI 代码页解析无 BOM 的 UTF-8 源，会把中文字节误当 DBCS 前导字节并报 C2001
# （"字符串字面量中的换行符"，实测 case_active_overflow.cpp(26,51) 等）。
# 这里只补项目基线里的 /utf-8，不给这些目标加优化参数、不改任何用例语义；
# GCC/Clang 路径不受影响（它们本来就按 UTF-8 解析），28 条反向用例的期望一字未动。
if(MSVC)
    set(IDF_CASE_TARGETS IDFixtureLegalCases)
    foreach(case_name IN LISTS IDF_CASE_NAMES)
        list(APPEND IDF_CASE_TARGETS fixture_expect_${case_name})
    endforeach()
    list(APPEND IDF_CASE_TARGETS
        fixture_expect_wrong_expectation
        fixture_expect_aux_assert_failure)
    foreach(case_target IN LISTS IDF_CASE_TARGETS)
        target_compile_options(${case_target} PRIVATE /utf-8)
    endforeach()

    # 静态 CRT：fixture 插件模块、IDFixtureAssembly 与两个原型 exe 都经 id_apply_performance_flags
    # 拿到 /MT（MultiThreaded），而合法对照的 OBJECT 库与 CaseControls 没有走该函数，默认仍是 /MD。
    # 两者一起链接会触发 LNK2038（RuntimeLibrary 不匹配：MT_StaticRelease vs MD_DynamicRelease）。
    # 这里只补"与链接桩同一套 C++ 运行时"这一条最小属性，不引入优化参数，也不改任何用例语义。
    # /utf-8 已在上面那个循环里给了合法对照 OBJECT 库；CaseControlMain.cpp 是同一个源目录里的
    # 无 BOM UTF-8 文件，且它自己就是一个编译目标，因此单独补一次。
    set(IDF_STATIC_CRT_TARGETS IDFixtureLegalCases FixtureCaseControls)
    foreach(crt_target IN LISTS IDF_STATIC_CRT_TARGETS)
        set_property(TARGET ${crt_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
    target_compile_options(FixtureCaseControls PRIVATE /utf-8)
endif()

set(IDF_NEGATIVE_CASES
    "self_dependency|SelfDependency,ambiguous_provider|AmbiguousProvider,missing_provider|MissingProvider,cycle|Cycle,hash_collision|NameHashCollision,duplicate_candidate|DuplicateCandidate,empty_inner_group|EmptyRequirementGroup,unknown_candidate|UnknownCandidate,active_overflow|TooManyActive,evidence_mismatch|EvidenceProviderMismatch,invalid_name|InvalidName,duplicate_plugin|DuplicatePlugin,too_many_requirements|TooManyRequirements,too_many_candidates|TooManyCandidates,too_many_catalog|TooManyCatalog,unknown_active|UnknownActive,duplicate_active|DuplicateActive,evidence_selection_mismatch|EvidenceSelectionMismatch,evidence_edge_mismatch|EvidenceEdgeMismatch,evidence_duplicate_selection|EvidenceSelectionMismatch,evidence_duplicate_binding|EvidenceProviderMismatch,evidence_duplicate_edge|EvidenceEdgeMismatch,evidence_capacity|EvidenceSelectionMismatch,self_dependency_prefix_collision|NameHashCollision,cycle_long|Cycle,name_boundaries|InvalidName,name_internal_nul|InvalidName,name_first_char|InvalidName")

# 批次参数里带上模式、构建目录、CWD、工具链与编译器身份，驱动器把它们写进日志头与索引，
# 使"某个模式的一批日志属于哪个构建目录/哪次命令"可以离线复核。
add_custom_target(FixtureNegativeCases
    COMMAND ${CMAKE_COMMAND}
        -DPROTOTYPE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        "-DBATCH_CWD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DMONOLITHIC=${ID_MONOLITHIC_BUILD}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        "-DRAW_LOG_DIR=${IDF_RAW_LOG_DIR}"
        "-DRAW_LOG_MODE=${IDF_RAW_LOG_MODE}"
        "-DCASES=${IDF_NEGATIVE_CASES}"
        -DWRONG_EXPECT_CASE=wrong_expectation
        -DAUX_FAIL_CASE=aux_assert_failure
        -DCONTROL_TARGET=IDFixtureLegalCases
        -P ${IDF_DIR}/cmake/RunNegativeCases.cmake
    VERBATIM
    # 进度行只留 ASCII：minGW make 会把 COMMENT 按本机 ANSI 码页回显，
    # 中文会在重定向到日志文件后变成乱码；判定行由驱动器自己写，始终是 UTF-8。
    COMMENT "fixture negative cases: 28 expect-fail targets + legal control + 2 driver self-checks")

# ---- 8. constexpr 成本阶梯 -------------------------------------------------
# 默认限制（GCC 16.2.0: -fconstexpr-ops-limit=33554432）下逐档测量；
# 每档单独成一个 TU，便于逐档记录编译时间与失败类别（见 prototype-p0-evidence.txt 第 3 节）。
set(IDF_SCALE_NAMES
    scale_active_128
    scale_32x32x1
    scale_64x32x4
    scale_max_legal_128x32x4
    scale_max_legal_128x32x8
    scale_probe)

# 超限档：保留为"默认限制下失败"的证据，预期编译失败，绝不参与默认构建。
# 名称里的 limit 表示它记录的是默认限制的边界，不是应当通过的用例。
set(IDF_SCALE_LIMIT_NAMES
    scale_limit_128x32x16)

foreach(scale_name IN LISTS IDF_SCALE_NAMES)
    add_library(fixture_${scale_name} OBJECT EXCLUDE_FROM_ALL ${IDF_DIR}/src/scale/${scale_name}.cpp)
    target_include_directories(fixture_${scale_name} PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
    target_link_libraries(fixture_${scale_name} PRIVATE ID)
endforeach()

foreach(scale_name IN LISTS IDF_SCALE_LIMIT_NAMES)
    add_library(fixture_${scale_name} OBJECT EXCLUDE_FROM_ALL ${IDF_DIR}/src/scale/${scale_name}.cpp)
    target_include_directories(fixture_${scale_name} PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
    target_link_libraries(fixture_${scale_name} PRIVATE ID)
endforeach()

# ---- 9. 压力目标：在**有依据的有限**常量求值预算下真实编译满组合 -------------
# 依据：GCC 16.2.0 默认 -fconstexpr-ops-limit=33554432 下 128/256/32/16 超限失败；
#       实测 67108864 仍失败、134217728 通过（raw/scale-required-limit.log）。
# 范围：只加在本原型的压力验证目标上，普通生产目标与普通 Benchmark 目标都不加；
#       参数按编译器 ID 限定（GCC 专有选项），MSVC / clang 未验证，不在此创建目标。
# 说明：这不是"把默认限制抹掉"——默认限制下的失败仍有独立目标 fixture_scale_limit_128x32x16 记录。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_library(fixture_scale_target_128x32x16 OBJECT EXCLUDE_FROM_ALL
        ${IDF_DIR}/src/scale/scale_target_128x32x16.cpp)
    target_include_directories(fixture_scale_target_128x32x16 PRIVATE ${IDF_INCLUDE_DIR} ${IDF_GEN_DIR})
    target_link_libraries(fixture_scale_target_128x32x16 PRIVATE ID)
    target_compile_options(fixture_scale_target_128x32x16 PRIVATE -fconstexpr-ops-limit=134217728)
    message(STATUS "fixture 压力目标 fixture_scale_target_128x32x16 使用 -fconstexpr-ops-limit=134217728（GCC 限定）")
else()
    message(STATUS "fixture 压力目标跳过：有限常量求值预算只在 GCC 16.2.0 上实测（当前编译器 ${CMAKE_CXX_COMPILER_ID} 未验证）")
endif()
