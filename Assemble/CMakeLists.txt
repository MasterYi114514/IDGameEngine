cmake_minimum_required(VERSION 3.20)

# =============================================================================
# Assemble：产品组装层（第五层）
#   - 只有在根工程设置了 ID_ASSEMBLY_PROFILE 时才会进入本目录（唯一开关，计划书 13.1）；
#   - 本层负责：读单一 JSON 清单、按 profile 解析唯一 provider、生成编译期输入与运行期绑定、
#     产出 IDAssemblyValidate / IDAssembly / IDGameEngine 三个产品目标；
#   - 产物一律落在本 build directory 下（out/、generated/），与其他构建目录互不覆盖；
#   - 本层不认识任何具体业务插件：catalog 条目一律是显式清单路径（集成 fixture 与业务插件 IDLog）；
#     新增插件只需在 catalog.cmake 增加一行路径，本文件不出现任何插件特判。
# =============================================================================

if(NOT ID_ASSEMBLY_PROFILE)
    message(FATAL_ERROR "Assemble/CMakeLists.txt 只应在 ID_ASSEMBLY_PROFILE 非空时进入（根 CMakeLists 已保证）")
endif()

set(ID_ASM_DIR          ${CMAKE_CURRENT_SOURCE_DIR})
set(ID_ASM_TEMPLATE_DIR ${ID_ASM_DIR}/templates)
set(ID_ASM_GEN_ROOT     ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ID_ASM_OUT_BIN      ${CMAKE_CURRENT_BINARY_DIR}/out/bin)
set(ID_ASM_OUT_LIB      ${CMAKE_CURRENT_BINARY_DIR}/out/lib)

file(MAKE_DIRECTORY ${ID_ASM_GEN_ROOT} ${ID_ASM_OUT_BIN} ${ID_ASM_OUT_LIB})

include(${ID_ASM_DIR}/cmake/IDReadCatalog.cmake)
include(${ID_ASM_DIR}/cmake/IDResolveSelection.cmake)
include(${ID_ASM_DIR}/cmake/IDGenerateAssembly.cmake)
include(${ID_ASM_DIR}/catalog.cmake)

set(ID_ASSEMBLY_PROFILE_PATH "${ID_ASM_DIR}/profiles/${ID_ASSEMBLY_PROFILE}.json")
if(NOT EXISTS "${ID_ASSEMBLY_PROFILE_PATH}")
    message(FATAL_ERROR "ID_ASSEMBLY_PROFILE=${ID_ASSEMBLY_PROFILE} 找不到 profile：${ID_ASSEMBLY_PROFILE_PATH}")
endif()

# ---- 1. 读取完整 catalog（schema 预检 + 身份唯一性） -------------------------
set(ID_ASM_CATALOG_PREFIX ID_ASM_CAT)
id_read_catalog(
    CATALOG     ${ID_ASSEMBLY_CATALOG_MANIFESTS}
    OUT_PREFIX  ${ID_ASM_CATALOG_PREFIX})
message(STATUS "assembly[${ID_ASSEMBLY_PROFILE}] catalog: ${ID_ASM_CAT_COUNT} manifests (${ID_ASM_CAT_NAMES})")

# ---- 2. profile 选中集合 + 逐需求唯一 provider + 真实链接边 -----------------
set(ID_ASM_RESOLVE_PREFIX ID_ASM_SEL)
id_resolve_selection(
    PROFILE         ${ID_ASSEMBLY_PROFILE_PATH}
    CATALOG_PREFIX  ${ID_ASM_CATALOG_PREFIX}
    OUT_PREFIX      ${ID_ASM_RESOLVE_PREFIX})

foreach(line IN LISTS ID_ASM_SEL_EVIDENCE_LINES)
    message(STATUS "assembly[${ID_ASM_SEL_ASSEMBLY_NAME}] ${line}")
endforeach()

# ---- 3. 生成目录与逐插件导出头（节点创建前必须先有导出头） -------------------
set(ID_ASM_GENERATED_DIR "${ID_ASM_GEN_ROOT}/${ID_ASM_SEL_ASSEMBLY_NAME}")
file(MAKE_DIRECTORY ${ID_ASM_GENERATED_DIR})

id_generate_export_headers(
    CATALOG_PREFIX   ${ID_ASM_CATALOG_PREFIX}
    GENERATED_DIR    ${ID_ASM_GENERATED_DIR}
    TEMPLATE_DIR     ${ID_ASM_TEMPLATE_DIR}
    SELECTED_INDICES ${ID_ASM_SEL_SELECTED_INDICES})

# ---- 4. 创建所有选中插件的 target（插件自己的 CMakeLists 只交付源码与 include） --
# 安装树里的公开头上限目录（安装规则见 cmake/IDInstall.cmake，两处必须一致）
set(ID_ASM_INSTALL_GENERATED_INCLUDE "${CMAKE_INSTALL_INCLUDEDIR}/IDGameEngine/generated/${ID_ASM_SEL_ASSEMBLY_NAME}")
foreach(index IN LISTS ID_ASM_SEL_SELECTED_INDICES)
    add_subdirectory("${ID_ASM_CAT_${index}_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/plugins/${ID_ASM_CAT_${index}_TARGET}")
endforeach()

# ---- 5. 全部 target 存在后，再按解析结果统一接线（只有真实 PUBLIC 上游） -----
id_assembly_apply_wiring(${ID_ASM_SEL_WIRING})

# 产品级全局属性：只登记"本产品实际有什么"，供宿主/安装层读取，
# 不构成第二份插件真相源（真相仍是 plugin.json + profile）。
set(ID_ASM_RUNTIME_TARGETS "")
foreach(selected_target IN LISTS ID_ASM_SEL_SELECTED_TARGETS)
    get_target_property(selected_kind ${selected_target} TYPE)
    if(selected_kind STREQUAL "SHARED_LIBRARY" OR selected_kind STREQUAL "MODULE_LIBRARY")
        list(APPEND ID_ASM_RUNTIME_TARGETS ${selected_target})
    endif()
endforeach()
# 逐插件公开头目录："目标|源码 include 目录"，供安装层按 catalog 位置复制，不重复插件名
set(ID_ASM_SELECTED_INCLUDE_ENTRIES "")
foreach(index IN LISTS ID_ASM_SEL_SELECTED_INDICES)
    list(APPEND ID_ASM_SELECTED_INCLUDE_ENTRIES
        "${ID_ASM_CAT_${index}_TARGET}|${ID_ASM_CAT_${index}_DIR}/include")
endforeach()

set_property(GLOBAL PROPERTY ID_PRODUCT_ASSEMBLY_NAME      "${ID_ASM_SEL_ASSEMBLY_NAME}")
set_property(GLOBAL PROPERTY ID_PRODUCT_SELECTED_TARGETS   "${ID_ASM_SEL_SELECTED_TARGETS}")
set_property(GLOBAL PROPERTY ID_PRODUCT_RUNTIME_TARGETS    "${ID_ASM_RUNTIME_TARGETS}")
set_property(GLOBAL PROPERTY ID_PRODUCT_GENERATED_DIR      "${ID_ASM_GENERATED_DIR}")
set_property(GLOBAL PROPERTY ID_PRODUCT_PROFILE_PATH       "${ID_ASSEMBLY_PROFILE_PATH}")
set_property(GLOBAL PROPERTY ID_PRODUCT_PLUGIN_INCLUDE_DIRS "${ID_ASM_SELECTED_INCLUDE_ENTRIES}")

# ---- 6. 生成编译期输入、校验 TU、绑定 TU、运行期声明与构建证据 ----------------
id_generate_assembly(
    CATALOG_PREFIX  ${ID_ASM_CATALOG_PREFIX}
    RESOLVE_PREFIX  ${ID_ASM_RESOLVE_PREFIX}
    GENERATED_DIR   ${ID_ASM_GENERATED_DIR}
    TEMPLATE_DIR    ${ID_ASM_TEMPLATE_DIR}
    PROFILE         ${ID_ASSEMBLY_PROFILE_PATH})

# ---- 7. 编译期校验目标：OBJECT，只依赖 ID 头，不链接任何业务插件（计划书 9.1） --
add_library(IDAssemblyValidate OBJECT ${ID_ASM_SEL_VALIDATE_SOURCE})
target_include_directories(IDAssemblyValidate PRIVATE "${ID_ASM_GENERATED_DIR}")
target_link_libraries(IDAssemblyValidate PRIVATE ID)
if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(IDAssemblyValidate)
endif()

# ---- 8. 运行期绑定 + 产品入口：STATIC，零插件也能独立编译（计划书 9.2） ------
add_library(IDAssembly STATIC
    ${ID_ASM_SEL_BINDING_SOURCE}
    ${ID_ASM_DIR}/src/EngineEntry.cpp)
target_include_directories(IDAssembly PRIVATE "${ID_ASM_GENERATED_DIR}" "${ID_ASM_DIR}/include")
target_link_libraries(IDAssembly PUBLIC ID)
if(ID_ASM_SEL_SELECTED_TARGETS)
    # 绑定表对选中回调用真实符号引用；未选中插件不在这份清单里
    target_link_libraries(IDAssembly PUBLIC ${ID_ASM_SEL_SELECTED_TARGETS})
endif()
# 结构错误必须在业务编译之前暴露：绑定目标依赖校验目标，反向依赖不允许
add_dependencies(IDAssembly IDAssemblyValidate)
if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(IDAssembly)
endif()
set_target_properties(IDAssembly PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ID_ASM_OUT_BIN}"
    LIBRARY_OUTPUT_DIRECTORY "${ID_ASM_OUT_BIN}"
    ARCHIVE_OUTPUT_DIRECTORY "${ID_ASM_OUT_LIB}")

# ---- 9. 产品消费目标：INTERFACE，传播 ID / IDAssembly / 公开能力 / 分配桩 -----
# include 区分构建树与安装树：安装后的消费方从 <prefix>/include/IDGameEngine 解析本头，
# 而构建树里仍指向源码目录（否则导出 target 会带绝对源路径，安装产物不可重定位）。
# 生成的选中身份/运行期声明头也属于交付物（计划书 13.4）：宿主用它读产品指纹与计划视图，
# 因此随消费目标一起传播；未选中插件的头与回调不会出现在这些生成物里。
add_library(IDGameEngine INTERFACE)
target_include_directories(IDGameEngine INTERFACE
    $<BUILD_INTERFACE:${ID_ASM_DIR}/include>
    $<BUILD_INTERFACE:${ID_ASM_GENERATED_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/IDGameEngine>
    $<INSTALL_INTERFACE:${ID_ASM_INSTALL_GENERATED_INCLUDE}>)
target_link_libraries(IDGameEngine INTERFACE ID IDAssembly)

# 宿主分配桩：只对 IDPluginStub 做保留，绝不全量保留插件库。
# STATIC 归并时最终程序里只有这一组替换符号；SHARED 模式下每个模块各自保留自己的桩。
if(MSVC)
    target_link_libraries(IDGameEngine INTERFACE IDPluginStub)
    target_link_options(IDGameEngine INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE:IDPluginStub>")
else()
    target_link_libraries(IDGameEngine INTERFACE "-Wl,--whole-archive" IDPluginStub "-Wl,--no-whole-archive")
endif()

# ---- 10. 消费 target 的最小冒烟（验证入口/链接/分配桩，不是 Benchmark 用例） ---
# 它是组装层的验证资产，不是产品本身：ship 交付构建用 -DID_BUILD_ASSEMBLY_SMOKE=OFF 关闭。
function(id_asm_isolate_outputs TARGET)
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ID_ASM_OUT_BIN}"
        LIBRARY_OUTPUT_DIRECTORY "${ID_ASM_OUT_BIN}"
        ARCHIVE_OUTPUT_DIRECTORY "${ID_ASM_OUT_LIB}")
endfunction()

function(id_asm_copy_kernel_runtime TARGET)
    if(NOT ID_MONOLITHIC_BUILD)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ID>" "${ID_ASM_OUT_BIN}/$<TARGET_FILE_NAME:ID>"
            COMMENT "copy ID kernel runtime next to ${TARGET}")
    endif()
endfunction()

if(ID_BUILD_ASSEMBLY_SMOKE)
    add_executable(IDAssemblySmoke ${ID_ASM_DIR}/tests/consumer/AssemblySmoke.cpp)
    target_link_libraries(IDAssemblySmoke PRIVATE IDGameEngine)
    if(COMMAND id_apply_performance_flags)
        id_apply_performance_flags(IDAssemblySmoke)
    endif()
    id_asm_isolate_outputs(IDAssemblySmoke)
    id_asm_copy_kernel_runtime(IDAssemblySmoke)
else()
    message(STATUS "assembly[${ID_ASM_SEL_ASSEMBLY_NAME}] 跳过 IDAssemblySmoke（ID_BUILD_ASSEMBLY_SMOKE=OFF）")
endif()

message(STATUS "assembly[${ID_ASM_SEL_ASSEMBLY_NAME}] generated: ${ID_ASM_GENERATED_DIR}")
message(STATUS "assembly[${ID_ASM_SEL_ASSEMBLY_NAME}] selected targets: ${ID_ASM_SEL_SELECTED_TARGETS}")
message(STATUS "assembly[${ID_ASM_SEL_ASSEMBLY_NAME}] evidence: ${ID_ASM_SEL_EVIDENCE_FILE}")
