# 通用组装生成器（计划书 8.1 / 8.2 / 8.4）
#
# 原则：
#   - 本文件不认识任何具体插件：所有内容都从 plugin.json + profile 的解析结果派生，
#     出现插件名字的地方一律来自变量，不出现分支特判；
#   - 生成物全部用 `configure_file` + `templates/*` 产出，不用 file(WRITE) 拼内容；
#   - 生成目录在本组装目标的 binary directory 下（`<build>/Assemble/generated/<assembly-id>/`），
#     不写工作台绝对路径，也不覆盖根工程共用的 bin/；
#   - 内容相同不刷新时间戳（configure_file 自带行为），JSON 与 profile 已加入 configure 依赖。
include_guard()

# 逐插件独立导出头：生成头名 = <Target>Export.generated.hpp
# （本仓库插件目标统一以 ID 开头，因此等价于计划书里的 ID<Plugin>Export.generated.hpp）
function(id_generate_export_headers)
    cmake_parse_arguments(R "" "CATALOG_PREFIX;GENERATED_DIR;TEMPLATE_DIR" "SELECTED_INDICES" ${ARGN})

    set(C "${R_CATALOG_PREFIX}")
    foreach(index IN LISTS R_SELECTED_INDICES)
        set(target "${${C}_${index}_TARGET}")
        set(token  "${${C}_${index}_TOKEN}")
        set(ID_ASM_EXPORT_TOKEN "${token}")
        configure_file("${R_TEMPLATE_DIR}/PluginExport.generated.hpp.in"
                       "${R_GENERATED_DIR}/${target}Export.generated.hpp"
                       @ONLY)
    endforeach()
endfunction()

# 生成组装输入头、校验 TU、绑定 TU、运行期声明头与构建证据
function(id_generate_assembly)
    cmake_parse_arguments(R ""
        "CATALOG_PREFIX;RESOLVE_PREFIX;GENERATED_DIR;TEMPLATE_DIR;PROFILE"
        "" ${ARGN})

    set(C   "${R_CATALOG_PREFIX}")
    set(Q   "${R_RESOLVE_PREFIX}")
    set(gen "${R_GENERATED_DIR}")

    file(MAKE_DIRECTORY "${gen}")

    # ---------------- 通用计数与产品信息 ----------------
    set(ID_ASM_CATALOG_COUNT   "${${C}_COUNT}")
    set(ID_ASM_SELECTED_COUNT  "${${Q}_SELECTED_COUNT}")
    set(ID_ASM_BINDING_COUNT   "${${Q}_BINDING_COUNT}")
    set(ID_ASM_EDGE_COUNT      "${${Q}_EDGE_COUNT}")
    set(ID_ASM_SLOT_COUNT      "${${Q}_SLOT_COUNT}")
    set(ID_ASM_PROVISION_COUNT "${${Q}_PROVISION_COUNT}")
    set(ID_ASM_ASSEMBLY_NAME   "${${Q}_ASSEMBLY_NAME}")

    # ---------------- catalog：每个条目由自己的清单容量决定类型参数 ----------------
    set(catalog_indices "")
    if(ID_ASM_CATALOG_COUNT GREATER 0)
        math(EXPR catalog_last "${ID_ASM_CATALOG_COUNT} - 1")
        foreach(index RANGE 0 ${catalog_last})
            list(APPEND catalog_indices ${index})
        endforeach()
    endif()

    set(ID_ASM_CATALOG_ENTRIES "")
    foreach(index IN LISTS catalog_indices)
        set(name            "${${C}_${index}_NAME}")
        set(target          "${${C}_${index}_TARGET}")
        set(nspace          "${${C}_${index}_NAMESPACE}")
        set(kind            "${${C}_${index}_KIND}")
        set(lifecycle       "${${C}_${index}_LIFECYCLE}")
        set(version         "${${C}_${index}_VERSION}")
        set(group_count     "${${C}_${index}_GROUP_COUNT}")
        set(candidate_total "${${C}_${index}_CANDIDATE_COUNT}")
        set(provides_count  "${${C}_${index}_PROVIDES_COUNT}")

        if(kind STREQUAL "interface")
            set(kind_enum "Interface")
        else()
            set(kind_enum "Module")
        endif()
        if(lifecycle STREQUAL "standard")
            set(lifecycle_enum "Standard")
        else()
            set(lifecycle_enum "None")
        endif()

        # 需求组：每组一条 std::array<std::string_view, N>，组长度可以不同
        set(group_expressions "")
        set(contract_text "")
        set(revision_text "")
        set(revision_begin 0)
        if(group_count GREATER 0)
            math(EXPR group_last "${group_count} - 1")
            foreach(group RANGE 0 ${group_last})
                set(candidates "${${C}_${index}_GROUP_${group}_CANDIDATES}")
                set(capability "${${C}_${index}_GROUP_${group}_CAPABILITY}")
                list(LENGTH candidates candidate_count)

                set(candidate_items "")
                set(candidate_position 0)
                foreach(candidate IN LISTS candidates)
                    string(APPEND candidate_items "\"${candidate}\", ")
                    set(min_revision "${${C}_${index}_GROUP_${group}_MIN_${candidate_position}}")
                    set(max_revision "${${C}_${index}_GROUP_${group}_MAX_${candidate_position}}")
                    string(APPEND revision_text
                        "                        ID::PluginCompileTime::CandidateRevision{ .name = \"${candidate}\", .min_revision = ${min_revision}, .max_revision = ${max_revision} },\n")
                    math(EXPR candidate_position "${candidate_position} + 1")
                endforeach()

                string(APPEND group_expressions "std::array<std::string_view, ${candidate_count}>{ ${candidate_items}}, ")
                math(EXPR revision_next "${revision_begin} + ${candidate_count}")
                string(APPEND contract_text
                    "                        ID::PluginCompileTime::RequirementContract{ .capability = \"${capability}\", .revision_begin = ${revision_begin}, .revision_count = ${candidate_count} },\n")
                set(revision_begin ${revision_next})
            endforeach()
            set(groups_expression "std::tuple{ ${group_expressions}}")
        else()
            set(groups_expression "std::tuple{}")
        endif()

        # provides：能力、接口修订、（可选）服务槽位
        set(provides_text "")
        if(provides_count GREATER 0)
            math(EXPR provides_last "${provides_count} - 1")
            foreach(provide RANGE 0 ${provides_last})
                set(capability   "${${C}_${index}_PROVIDE_${provide}_CAPABILITY}")
                set(api_revision "${${C}_${index}_PROVIDE_${provide}_REVISION}")
                set(slot         "${${C}_${index}_PROVIDE_${provide}_SLOT}")
                if(slot EQUAL -1)
                    set(slot_expression "ID::PluginCompileTime::kNoSlot")
                else()
                    set(slot_expression "${slot}")
                endif()
                string(APPEND provides_text
                    "                        ID::PluginCompileTime::Provision{ .capability = \"${capability}\", .api_revision = ${api_revision}, .slot = ${slot_expression} },\n")
            endforeach()
        endif()

        string(APPEND ID_ASM_CATALOG_ENTRIES
"            ID::PluginCompileTime::PluginCatalogEntry<${group_count}, ${candidate_total}, ${provides_count}>{
                ID::PluginManager::declare_dependencies(ID::PluginManager::register_plugin(\"${name}\"), ${groups_expression}),
                ID::PluginCompileTime::PluginMetadata<${group_count}, ${candidate_total}, ${provides_count}>{
                    .target         = \"${target}\",
                    .cpp_namespace  = \"${nspace}\",
                    .kind           = ID::PluginCompileTime::PluginKind::${kind_enum},
                    .lifecycle      = ID::PluginCompileTime::PluginLifecycle::${lifecycle_enum},
                    .version        = ${version},
                    .contract_count = ${group_count},
                    .provides_count = ${provides_count},
                    .contracts      = {
${contract_text}                    },
                    .revisions      = {
${revision_text}                    },
                    .provides       = {
${provides_text}                    },
                },
            },
")
    endforeach()

    # ---------------- 选中身份 ----------------
    set(ID_ASM_SELECTED_ENTRIES "")
    set(selected_indices "${${Q}_SELECTED_INDICES}")
    foreach(index IN LISTS selected_indices)
        string(APPEND ID_ASM_SELECTED_ENTRIES
            "        ID::PluginManager::register_plugin(\"${${C}_${index}_NAME}\"),\n")
    endforeach()

    # ---------------- 构建证据（CMake 派生） ----------------
    set(ID_ASM_EVIDENCE_BODY "")
    set(record 0)

    set(selected_evidence "${${Q}_SELECTED_EVIDENCE}")
    foreach(item IN LISTS selected_evidence)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 name)
        list(GET parts 1 target)
        list(GET parts 2 macro)
        list(GET parts 3 plugin_id)
        string(APPEND ID_ASM_EVIDENCE_BODY
            "        evidence.selected[${record}] = ID::PluginCompileTime::SelectedEvidence{ .name = \"${name}\", .target = \"${target}\", .macro = \"${macro}\", .plugin_id = ${plugin_id} };\n")
        math(EXPR record "${record} + 1")
    endforeach()

    set(record 0)
    foreach(item IN LISTS ${Q}_BINDINGS)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 dependent)
        list(GET parts 1 provider)
        list(GET parts 2 requirement)
        string(APPEND ID_ASM_EVIDENCE_BODY
            "        evidence.bindings[${record}] = ID::PluginCompileTime::BindingEvidence{ .dependent = \"${dependent}\", .provider = \"${provider}\", .requirement = ${requirement} };\n")
        math(EXPR record "${record} + 1")
    endforeach()

    set(record 0)
    foreach(item IN LISTS ${Q}_EDGES)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 from)
        list(GET parts 1 to)
        string(APPEND ID_ASM_EVIDENCE_BODY
            "        evidence.edges[${record}] = ID::PluginCompileTime::EdgeEvidence{ .from = \"${from}\", .to = \"${to}\" };\n")
        math(EXPR record "${record} + 1")
    endforeach()

    set(record 0)
    foreach(item IN LISTS ${Q}_SLOTS)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 owner)
        list(GET parts 1 capability)
        list(GET parts 2 slot)
        string(APPEND ID_ASM_EVIDENCE_BODY
            "        evidence.slots[${record}] = ID::PluginCompileTime::SlotEvidence{ .owner = \"${owner}\", .capability = \"${capability}\", .slot = ${slot} };\n")
        math(EXPR record "${record} + 1")
    endforeach()

    set(record 0)
    foreach(item IN LISTS ${Q}_RUNTIME)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 name)
        list(GET parts 1 capability)
        list(GET parts 2 plugin_id)
        list(GET parts 3 api_revision)
        string(APPEND ID_ASM_EVIDENCE_BODY
            "        evidence.runtime[${record}] = ID::PluginCompileTime::RuntimeBindingEvidence{ .name = \"${name}\", .capability = \"${capability}\", .plugin_id = ${plugin_id}, .api_revision = ${api_revision} };\n")
        math(EXPR record "${record} + 1")
    endforeach()

    # ---------------- 运行期绑定：只含选中模块的生命周期声明与回调 ----------------
    set(ID_ASM_BINDING_INCLUDES "")
    set(ID_ASM_BINDING_NAMES "")
    set(ID_ASM_BINDING_CALLBACKS "")
    foreach(index IN LISTS selected_indices)
        set(name      "${${C}_${index}_NAME}")
        set(target    "${${C}_${index}_TARGET}")
        set(nspace    "${${C}_${index}_NAMESPACE}")
        set(kind      "${${C}_${index}_KIND}")
        set(lifecycle "${${C}_${index}_LIFECYCLE}")

        string(APPEND ID_ASM_BINDING_NAMES "            \"${name}\",\n")

        if(kind STREQUAL "module" AND lifecycle STREQUAL "standard")
            # 回调地址用清单里声明的命名空间限定：生成器不认识具体插件，只按约定拼名字
            string(APPEND ID_ASM_BINDING_INCLUDES "#include \"${target}Lifecycle.hpp\"\n")
            string(APPEND ID_ASM_BINDING_CALLBACKS
                "            ID::PluginRuntimeCallbacks{ &${nspace}::${target}OnRegister, &${nspace}::${target}OnInit, &${nspace}::${target}OnShutdown, &${nspace}::${target}OnUnload },\n")
        else()
            # interface（纯头文件）与 lifecycle=none：没有运行期回调，身份仍保留在计划里
            string(APPEND ID_ASM_BINDING_CALLBACKS "            ID::PluginRuntimeCallbacks{},\n")
        endif()
    endforeach()

    # ---------------- 确定性序列：产品指纹的输入（计划书 13.3） ----------------
    # 排序口径与 C++ 侧的 canonical 顺序一致：名字/能力名按 ASCII 字节序，槽位按编号补零。
    # catalog 枚举顺序、profile 书写顺序、候选顺序一律不进入本段。
    set(canonical_selected "")
    foreach(item IN LISTS ${Q}_SELECTED_EVIDENCE)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 entry_name)
        list(GET parts 1 entry_target)
        list(GET parts 2 entry_macro)
        list(GET parts 3 entry_plugin_id)
        list(APPEND canonical_selected "${entry_name} target=${entry_target} macro=${entry_macro} id=${entry_plugin_id}")
    endforeach()
    list(SORT canonical_selected)

    set(canonical_provision "")
    foreach(item IN LISTS ${Q}_RUNTIME)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 entry_name)
        list(GET parts 1 entry_capability)
        list(GET parts 3 entry_revision)
        list(APPEND canonical_provision "${entry_name}.${entry_capability} revision=${entry_revision}")
    endforeach()
    list(SORT canonical_provision)

    set(canonical_binding "")
    foreach(item IN LISTS ${Q}_BINDINGS)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 entry_dependent)
        list(GET parts 1 entry_provider)
        list(GET parts 2 entry_requirement)
        list(APPEND canonical_binding "${entry_dependent} -> ${entry_provider} requirement=${entry_requirement}")
    endforeach()
    list(SORT canonical_binding)

    set(canonical_slot "")
    foreach(item IN LISTS ${Q}_SLOTS)
        string(REPLACE "|" ";" parts "${item}")
        list(GET parts 0 entry_owner)
        list(GET parts 1 entry_capability)
        list(GET parts 2 entry_slot)
        if(entry_slot LESS 10)
            set(entry_slot_text "0${entry_slot}")
        else()
            set(entry_slot_text "${entry_slot}")
        endif()
        list(APPEND canonical_slot "${entry_slot_text} ${entry_owner}.${entry_capability}")
    endforeach()
    list(SORT canonical_slot)

    set(canonical_lines "")
    foreach(entry IN LISTS canonical_selected)
        list(APPEND canonical_lines "canonical-selected  ${entry}")
    endforeach()
    foreach(entry IN LISTS canonical_provision)
        list(APPEND canonical_lines "canonical-provide   ${entry}")
    endforeach()
    foreach(entry IN LISTS canonical_binding)
        list(APPEND canonical_lines "canonical-binding   ${entry}")
    endforeach()
    foreach(entry IN LISTS canonical_slot)
        list(APPEND canonical_lines "canonical-slot      ${entry}")
    endforeach()
    if(canonical_lines)
        string(REPLACE ";" "\n" ID_ASM_CANONICAL_TEXT "${canonical_lines}")
    else()
        set(ID_ASM_CANONICAL_TEXT "canonical-selected  <空产品：未选中任何插件>")
    endif()

    # ---------------- 落盘：全部走 templates + configure_file ----------------
    configure_file("${R_TEMPLATE_DIR}/Assembly.generated.hpp.in"
                   "${gen}/IDAssembly.generated.hpp"
                   @ONLY)

    configure_file("${R_TEMPLATE_DIR}/AssemblyValidate.generated.cpp.in"
                   "${gen}/IDAssemblyValidate.generated.cpp"
                   @ONLY)

    configure_file("${R_TEMPLATE_DIR}/AssemblyBinding.generated.cpp.in"
                   "${gen}/IDAssemblyBinding.generated.cpp"
                   @ONLY)

    configure_file("${R_TEMPLATE_DIR}/AssemblyRuntime.generated.hpp.in"
                   "${gen}/IDAssemblyRuntime.generated.hpp"
                   @ONLY)

    set(ID_ASM_PROFILE "${R_PROFILE}")
    get_filename_component(ID_ASM_PROFILE "${ID_ASM_PROFILE}" NAME)
    if(ID_MONOLITHIC_BUILD)
        set(ID_ASM_MODE "STATIC")
    else()
        set(ID_ASM_MODE "SHARED")
    endif()
    set(ID_ASM_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    # 证据列表按行落盘：CMake 列表用 ";" 分隔，直接代入会挤成一行
    string(REPLACE ";" "\n" ID_ASM_EVIDENCE_LINES "${${Q}_EVIDENCE_LINES}")
    configure_file("${R_TEMPLATE_DIR}/AssemblyEvidence.txt.in"
                   "${gen}/IDAssemblyEvidence.txt"
                   @ONLY)

    # 回传生成物路径，供目标创建使用
    set(${Q}_GENERATED_HEADER  "${gen}/IDAssembly.generated.hpp"          PARENT_SCOPE)
    set(${Q}_VALIDATE_SOURCE   "${gen}/IDAssemblyValidate.generated.cpp"  PARENT_SCOPE)
    set(${Q}_BINDING_SOURCE    "${gen}/IDAssemblyBinding.generated.cpp"   PARENT_SCOPE)
    set(${Q}_RUNTIME_HEADER    "${gen}/IDAssemblyRuntime.generated.hpp"   PARENT_SCOPE)
    set(${Q}_EVIDENCE_FILE     "${gen}/IDAssemblyEvidence.txt"            PARENT_SCOPE)
endfunction()

# 逐插件构建入口：只描述本插件自己的源码；名字/依赖/能力/槽位一律来自 catalog
# 用法：id_assembly_add_module(<target> [SOURCES <源文件...>] [PRIVATE_INCLUDE_DIRS <目录...>])
#   PRIVATE_INCLUDE_DIRS 只对本插件自身的编译生效（PRIVATE 语义），绝不进入下游编译；
#   用于私有第三方头（如 IDLog 的 vendor/spdlog/include），不是公开 include 的替代品。
function(id_assembly_add_module TARGET)
    cmake_parse_arguments(M "" "" "SOURCES;PRIVATE_INCLUDE_DIRS" ${ARGN})

    set(P "${ID_ASM_CATALOG_PREFIX}")
    if(NOT DEFINED ${P}_TARGET_${TARGET}_INDEX)
        message(FATAL_ERROR "id_assembly_add_module(${TARGET}): 该目标不在 catalog 中；未登记的插件不得进入本产品")
    endif()

    set(index   "${${P}_TARGET_${TARGET}_INDEX}")
    set(kind    "${${P}_${index}_KIND}")
    set(token   "${${P}_${index}_TOKEN}")
    set(include "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(NOT IS_DIRECTORY "${include}")
        message(FATAL_ERROR "插件 ${TARGET} 缺少公开 include 目录：${include}")
    endif()

    # 公开 include 分构建树/安装树两种来源：安装树里的路径必须是相对 prefix 的，
    # 否则导出 target 会带着源码目录的绝对路径，安装产物不可重定位（计划书 13.4）。
    set(install_include_args
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/IDGameEngine/plugins/${TARGET}>"
        "$<INSTALL_INTERFACE:${ID_ASM_INSTALL_GENERATED_INCLUDE}>")

    if(kind STREQUAL "interface")
        if(M_SOURCES)
            message(FATAL_ERROR "interface 插件 ${TARGET} 是纯头文件插件，不得编译源文件")
        endif()
        if(M_PRIVATE_INCLUDE_DIRS)
            message(FATAL_ERROR "interface 插件 ${TARGET} 是纯头文件插件，不得声明私有 include 目录")
        endif()
        add_library(${TARGET} INTERFACE)
        target_compile_features(${TARGET} INTERFACE cxx_std_20)
        target_include_directories(${TARGET} INTERFACE
            "$<BUILD_INTERFACE:${include}>"
            "$<BUILD_INTERFACE:${ID_ASM_GENERATED_DIR}>"
            ${install_include_args})
        target_compile_definitions(${TARGET} INTERFACE "ID_USE_PLUGIN_${token}=1")
        target_link_libraries(${TARGET} INTERFACE ID)
        # interface 插件不产出任何二进制，因此不设置输出目录（INTERFACE 库没有实体产物）
        return()
    endif()

    if(ID_MONOLITHIC_BUILD)
        add_library(${TARGET} STATIC ${M_SOURCES})
    else()
        add_library(${TARGET} SHARED ${M_SOURCES})
        # build 宏只在插件自身 PRIVATE：它决定本模块导出头里的 export/import，不向下游传播
        target_compile_definitions(${TARGET} PRIVATE "ID_${token}_BUILD_DLL=1")
    endif()

    target_compile_features(${TARGET} PUBLIC cxx_std_20)
    target_include_directories(${TARGET} PUBLIC
        "$<BUILD_INTERFACE:${include}>"
        "$<BUILD_INTERFACE:${ID_ASM_GENERATED_DIR}>"
        ${install_include_args})
    if(M_PRIVATE_INCLUDE_DIRS)
        # 私有第三方头只对本插件自身的编译生效：PRIVATE 不进 usage requirements，
        # 因此下游既拿不到 vendor 路径，也不会被它污染（计划书 8.3 的"上游只交付能力"）
        target_include_directories(${TARGET} PRIVATE ${M_PRIVATE_INCLUDE_DIRS})
    endif()
    # 身份宏是 PUBLIC usage requirement：下游只能通过真实上游 target 拿到它，不允许全局注入
    target_compile_definitions(${TARGET} PUBLIC "ID_USE_PLUGIN_${token}=1")
    target_link_libraries(${TARGET} PUBLIC ID)

    if(NOT ID_MONOLITHIC_BUILD)
        # 分配桩保留：普通归档链接只在“本模块真的引用了 operator new”时才抽取桩对象。
        # 只对 IDPluginStub 做 whole-archive，绝不对插件库做全量保留（计划书 9.4）。
        # STATIC 归并时不在这里链接桩：最终程序由 IDGameEngine 统一做一次保留，
        # 否则同一份替换符号会被重复引入。
        if(MSVC)
            # /WHOLEARCHIVE 只是链接器选项，不会产生库依赖，因此 vcxproj 里只有 AdditionalOptions，
            # 不建立 ProjectReference：干净构建目录里单独构建本模块会得到
            # LNK1356（找不到指定为已整体存档的库）。
            # 所以先登记真实库依赖（建立构建顺序，并把桩纳入增量重链接的输入），再叠加整档保留选项。
            target_link_libraries(${TARGET} PRIVATE IDPluginStub)
            target_link_options(${TARGET} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:IDPluginStub>")
        else()
            target_link_libraries(${TARGET} PRIVATE "-Wl,--whole-archive" IDPluginStub "-Wl,--no-whole-archive")
        endif()
    endif()

    if(COMMAND id_apply_performance_flags)
        id_apply_performance_flags(${TARGET})
    endif()

    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ID_ASM_OUT_BIN}"
        LIBRARY_OUTPUT_DIRECTORY "${ID_ASM_OUT_BIN}"
        ARCHIVE_OUTPUT_DIRECTORY "${ID_ASM_OUT_LIB}")
endfunction()

# 把解析结果接到真实目标上：只连真实 PUBLIC 上游，不注入与插件无关的身份
function(id_assembly_apply_wiring)
    foreach(record IN LISTS ARGN)
        string(REPLACE "|" ";" parts "${record}")
        list(GET parts 0 dependent_target)
        list(GET parts 1 provider_target)
        if(NOT TARGET ${dependent_target} OR NOT TARGET ${provider_target})
            message(FATAL_ERROR "接线失败：目标不存在 ${dependent_target} <- ${provider_target}")
        endif()
        target_link_libraries(${dependent_target} PUBLIC ${provider_target})
    endforeach()
endfunction()
