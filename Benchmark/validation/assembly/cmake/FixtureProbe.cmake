# 组装 fixture 的最小探针（configure 期，供 Benchmark/validation/assembly/fixtures/<case> 使用）
#
# 职责：把"一个 fixture 目录 + 一份临时 profile"跑成一次 configure 期判定，并输出机器可读的
# `FIXTURE-PROBE` 行，交给 cmake/RunFixtureMatrix.cmake 逐条断言。
#
# 关键契约：
#   1. **不复制解析逻辑**：catalog 读取、profile 选择、生成物派生全部通过 include() 复用
#      Assemble/cmake/IDReadCatalog.cmake、IDResolveSelection.cmake、IDGenerateAssembly.cmake；
#      本文件只做"写临时 profile + 调用 + 汇报"，不重新实现任何匹配/去重/指纹算法。
#   2. **不创建 target、不产出二进制**：本资产只做 configure 期语义判定与生成物静态校验。
#      插件 target 创建、真实 PUBLIC 链接边、产物与运行期行为属既有 IDAssemblyValidationRun 的职责。
#   3. **临时 profile 由本探针生成**：`enabled_plugins` 的书写顺序就是 profile 顺序，
#      因此"profile 重排"用例只要改自己的 ENABLED 顺序即可。也可以用 ID_FIXTURE_PROFILE
#      显式指向一份外部临时 profile（-DID_FIXTURE_PROFILE=<path>）来覆盖。
#   4. 断言口径只看**生成物**（证据文件头部计数、canonical 段、group_allows_multi 赋值、
#      <Target>Matched.generated.hpp 的宏），不由探针重新推导一遍语义。
#
# 用法（每个 fixture 的 CMakeLists.txt）：
#   cmake_minimum_required(VERSION 3.20)
#   project(<case> LANGUAGES NONE)
#   include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")
#   id_fixture_probe(NAME <case>
#                    CATALOG <plugin.json...>
#                    ENABLED <插件名...>)
include_guard()

# Assemble 目录：默认从本文件位置推导（<engine>/Benchmark/validation/assembly/cmake -> <engine>/Assemble）；
# 也可以用 -DID_FIXTURE_ASSEMBLE_DIR=<engine>/Assemble 显式覆盖。
if(NOT DEFINED ID_FIXTURE_ASSEMBLE_DIR)
    get_filename_component(_fixture_engine_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
    set(ID_FIXTURE_ASSEMBLE_DIR "${_fixture_engine_root}/Assemble")
endif()

# 标定探针自身的输入；缺一项就报错，不静默用默认值继续
foreach(_fixture_module IDReadCatalog IDResolveSelection IDGenerateAssembly)
    if(NOT EXISTS "${ID_FIXTURE_ASSEMBLE_DIR}/cmake/${_fixture_module}.cmake")
        message(FATAL_ERROR "组装 fixture 探针找不到 Assemble 模块：${ID_FIXTURE_ASSEMBLE_DIR}/cmake/${_fixture_module}.cmake")
    endif()
endforeach()
if(NOT EXISTS "${ID_FIXTURE_ASSEMBLE_DIR}/templates/AssemblyEvidence.txt.in")
    message(FATAL_ERROR "组装 fixture 探针找不到 Assemble 模板目录：${ID_FIXTURE_ASSEMBLE_DIR}/templates")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../fixtures/support/Manifests.cmake")

# 把一条 FIXTURE-PROBE 记录写到 configure 输出（驱动按前缀逐行解析）
function(id_fixture_emit text)
    message(STATUS "FIXTURE-PROBE ${text}")
endfunction()

# 生成临时 profile：enabled_plugins 顺序 = 传入顺序（profile 顺序语义）
function(id_fixture_write_profile out_path assembly_name)
    set(entries "")
    foreach(name IN LISTS ARGN)
        string(APPEND entries "        \"${name}\",\n")
    endforeach()
    if(NOT entries STREQUAL "")
        # 去掉最后一个逗号：手写 JSON 必须自洽，不能靠解析器容错
        string(REGEX REPLACE ",\n$" "\n" entries "${entries}")
    endif()
    set(text "{\n    \"schema_version\": 1,\n    \"assembly_name\": \"${assembly_name}\",\n    \"enabled_plugins\": [\n${entries}    ]\n}\n")
    file(WRITE "${out_path}" "${text}")
endfunction()

# 从生成头里取出 group_allows_multi 的逐项赋值：编址 = 选中槽位 × kMaxRequirements + 需求组
function(id_fixture_emit_group_multi generated_header)
    if(NOT EXISTS "${generated_header}")
        return()
    endif()
    file(STRINGS "${generated_header}" assignments REGEX "group_allows_multi\\[")
    foreach(line IN LISTS assignments)
        string(REGEX MATCH "group_allows_multi\\[([0-9]+) \\* ID::PluginCompileTime::kMaxRequirements \\+ ([0-9]+)\\]" matched "${line}")
        if(matched STREQUAL "")
            id_fixture_emit("group-multi-unparsed ${line}")
        else()
            math(EXPR slot "${CMAKE_MATCH_1}")
            set(group "${CMAKE_MATCH_2}")
            id_fixture_emit("allows-multi slot=${slot} group=${group} value=1")
        endif()
    endforeach()
endfunction()

# 从 <Target>Matched.generated.hpp 里取出逐组命中条数与逐成员宏
# 形参：generated_dir, target（= <Target>Matched.generated.hpp 的前缀）, consumer token
function(id_fixture_emit_availability generated_dir target token)
    set(header "${generated_dir}/${target}Matched.generated.hpp")
    if(NOT EXISTS "${header}")
        id_fixture_emit("availability-header-missing target=${target}")
        return()
    endif()
    id_fixture_emit("availability-target target=${target} token=${token}")
    file(STRINGS "${header}" defines REGEX "^#define ID_MATCHED_")

    # ID_MATCHED_TOTAL_<TOKEN> <n>
    foreach(line IN LISTS defines)
        if(line MATCHES "^#define ID_MATCHED_TOTAL_${token} ([0-9]+)$")
            id_fixture_emit("availability-total token=${token} total=${CMAKE_MATCH_1}")
        elseif(line MATCHES "^#define ID_MATCHED_COUNT_${token}_G([0-9]+) ([0-9]+)$")
            id_fixture_emit("availability-count token=${token} group=${CMAKE_MATCH_1} count=${CMAKE_MATCH_2}")
        elseif(line MATCHES "^#define ID_MATCHED_MEMBER_${token}_G([0-9]+)_([A-Z0-9_]+) 1$")
            id_fixture_emit("availability-member token=${token} group=${CMAKE_MATCH_1} provider=${CMAKE_MATCH_2}")
        endif()
    endforeach()
endfunction()

# id_fixture_probe(NAME <case> CATALOG <manifest...> ENABLED <name...> [PROFILE <path>])
function(id_fixture_probe)
    cmake_parse_arguments(P "" "NAME;PROFILE" "CATALOG;ENABLED" ${ARGN})

    if(NOT P_NAME OR NOT P_CATALOG OR NOT P_ENABLED)
        message(FATAL_ERROR "id_fixture_probe: NAME / CATALOG / ENABLED 均为必填（当前 NAME='${P_NAME}'）")
    endif()

    foreach(manifest IN LISTS P_CATALOG)
        if(NOT EXISTS "${manifest}")
            message(FATAL_ERROR "组装 fixture ${P_NAME} 的 catalog 条目不存在：${manifest}")
        endif()
    endforeach()

    # ---- 临时 profile ----
    # 优先级：函数实参 PROFILE > -DID_FIXTURE_PROFILE=<外部临时 profile> > 由 ENABLED 生成的临时 profile。
    # 外部覆盖用于"同一份清单换 profile 复跑"（例如同一 case 只换 enabled_plugins 顺序或子集）。
    if(NOT P_PROFILE AND NOT "${ID_FIXTURE_PROFILE}" STREQUAL "")
        set(P_PROFILE "${ID_FIXTURE_PROFILE}")
    endif()
    if(P_PROFILE)
        if(NOT EXISTS "${P_PROFILE}")
            message(FATAL_ERROR "组装 fixture ${P_NAME} 指定的临时 profile 不存在：${P_PROFILE}")
        endif()
        set(profile_path "${P_PROFILE}")
    else()
        set(profile_path "${CMAKE_BINARY_DIR}/profile.generated.json")
        id_fixture_write_profile("${profile_path}" "${P_NAME}" ${P_ENABLED})
    endif()

    # ---- 复用 Assemble 的解析/选择/生成模块 ----
    include("${ID_FIXTURE_ASSEMBLE_DIR}/cmake/IDReadCatalog.cmake")
    include("${ID_FIXTURE_ASSEMBLE_DIR}/cmake/IDResolveSelection.cmake")
    include("${ID_FIXTURE_ASSEMBLE_DIR}/cmake/IDGenerateAssembly.cmake")

    # 构建模式只影响证据头的一行文本；本资产不做真实构建，固定按 SHARED 记录
    if(NOT DEFINED ID_MONOLITHIC_BUILD)
        set(ID_MONOLITHIC_BUILD OFF)
    endif()

    set(ID_FIXTURE_CATALOG_PREFIX IDFIXTURECAT)
    set(ID_FIXTURE_RESOLVE_PREFIX IDFIXTURESEL)
    id_read_catalog(CATALOG ${P_CATALOG} OUT_PREFIX ${ID_FIXTURE_CATALOG_PREFIX})

    id_fixture_emit("case ${P_NAME}")
    id_fixture_emit("profile ${profile_path}")
    id_fixture_emit("catalog-count ${${ID_FIXTURE_CATALOG_PREFIX}_COUNT}")

    # 逐清单 schema 版本：schema1 / schema2 的判定差异必须能从探针输出直接看出
    math(EXPR _fixture_catalog_last "${${ID_FIXTURE_CATALOG_PREFIX}_COUNT} - 1")
    if(_fixture_catalog_last GREATER_EQUAL 0)
        foreach(_fixture_index RANGE 0 ${_fixture_catalog_last})
            id_fixture_emit("catalog-entry name=${${ID_FIXTURE_CATALOG_PREFIX}_${_fixture_index}_NAME} schema=${${ID_FIXTURE_CATALOG_PREFIX}_${_fixture_index}_SCHEMA} groups=${${ID_FIXTURE_CATALOG_PREFIX}_${_fixture_index}_GROUP_COUNT}")
        endforeach()
    endif()

    id_resolve_selection(PROFILE "${profile_path}"
                         CATALOG_PREFIX ${ID_FIXTURE_CATALOG_PREFIX}
                         OUT_PREFIX    ${ID_FIXTURE_RESOLVE_PREFIX})

    set(generated_dir "${CMAKE_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${generated_dir}")

    id_generate_export_headers(CATALOG_PREFIX ${ID_FIXTURE_CATALOG_PREFIX}
        GENERATED_DIR "${generated_dir}"
        TEMPLATE_DIR  "${ID_FIXTURE_ASSEMBLE_DIR}/templates"
        SELECTED_INDICES ${${ID_FIXTURE_RESOLVE_PREFIX}_SELECTED_INDICES})

    id_generate_assembly(CATALOG_PREFIX ${ID_FIXTURE_CATALOG_PREFIX}
        RESOLVE_PREFIX  ${ID_FIXTURE_RESOLVE_PREFIX}
        GENERATED_DIR   "${generated_dir}"
        TEMPLATE_DIR    "${ID_FIXTURE_ASSEMBLE_DIR}/templates"
        PROFILE         "${profile_path}")

    # ---- 生成物汇报（全部来自生成物本身，探针不重算语义） ----
    set(evidence_file "${${ID_FIXTURE_RESOLVE_PREFIX}_EVIDENCE_FILE}")
    id_fixture_emit("evidence ${evidence_file}")
    id_fixture_emit("generated-dir ${generated_dir}")
    id_fixture_emit("counts selected=${${ID_FIXTURE_RESOLVE_PREFIX}_SELECTED_COUNT} bindings=${${ID_FIXTURE_RESOLVE_PREFIX}_BINDING_COUNT} edges=${${ID_FIXTURE_RESOLVE_PREFIX}_EDGE_COUNT} slots=${${ID_FIXTURE_RESOLVE_PREFIX}_SLOT_COUNT} provisions=${${ID_FIXTURE_RESOLVE_PREFIX}_PROVISION_COUNT}")

    # canonical 段 = 产品指纹的 CMake 可派生输入（排序后，与 catalog/profile/候选顺序无关）
    if(EXISTS "${evidence_file}")
        file(STRINGS "${evidence_file}" canonical_lines REGEX "^canonical-")
        set(canonical_text "")
        foreach(line IN LISTS canonical_lines)
            string(APPEND canonical_text "${line}\n")
            id_fixture_emit("canonical ${line}")
        endforeach()
        string(SHA256 canonical_hash "${canonical_text}")
        id_fixture_emit("canonical-sha256 ${canonical_hash}")
    else()
        id_fixture_emit("evidence-missing ${evidence_file}")
    endif()

    id_fixture_emit_group_multi("${generated_dir}/IDAssembly.generated.hpp")

    foreach(_fixture_selected IN LISTS ${ID_FIXTURE_RESOLVE_PREFIX}_SELECTED_INDICES)
        set(_fixture_target "${${ID_FIXTURE_CATALOG_PREFIX}_${_fixture_selected}_TARGET}")
        set(_fixture_token  "${${ID_FIXTURE_CATALOG_PREFIX}_${_fixture_selected}_TOKEN}")
        id_fixture_emit("selected name=${${ID_FIXTURE_CATALOG_PREFIX}_${_fixture_selected}_NAME} target=${_fixture_target} macro=ID_USE_PLUGIN_${_fixture_token}")
        id_fixture_emit_availability("${generated_dir}" "${_fixture_target}" "${_fixture_token}")
    endforeach()

    # 真实 PUBLIC 链接边的传递闭包：驱动据此给编译用例注入"consumer 实际能看到"的身份宏
    foreach(_fixture_wiring IN LISTS ${ID_FIXTURE_RESOLVE_PREFIX}_WIRING)
        string(REPLACE "|" ";" _fixture_wiring_parts "${_fixture_wiring}")
        list(GET _fixture_wiring_parts 0 _fixture_dependent)
        list(GET _fixture_wiring_parts 1 _fixture_provider)
        id_fixture_emit("wiring dependent=${_fixture_dependent} provider=${_fixture_provider}")
    endforeach()

    id_fixture_emit("done ${P_NAME}")
endfunction()
