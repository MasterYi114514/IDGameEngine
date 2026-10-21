# 读取插件清单并做 schema 预检（计划书 8.1 / 8.2 第 2 步、第 7 节格式与身份校验）
#
# 职责：
#   1. 按显式路径读取每份 plugin.json，缺文件/缺字段/未知 schema 直接拒绝；
#   2. 校验名字、CMake 目标、C++ 命名空间的唯一性，以及 target -> ID_USE_PLUGIN_<TARGET_ID>
#      归一化后的冲突（两个目标映射到同一个身份宏必须拒绝）；
#   3. 把每份清单整理成“扁平变量表”回传，供选profile解析与生成器使用。
#
# 明确不做的事：
#   - 不在这里决定选中集合（profile 决定），也不做逐需求 provider 解析；
#   - 不重复登记名字与依赖：所有内容都来自 plugin.json；
#   - 不做拓扑排序：最终顺序由 C++ 侧 sort_and_validate 产出（计划书 8.5）。
include_guard()

# target -> 身份宏 token：大写，非 [A-Z0-9_] 变下划线
# 必须与 ID/include/Kernel/PluginCompileTime.hpp 的 identity_macro_matches 归一化口径逐字节一致
function(id_asm_target_token TARGET OUT_VAR)
    string(TOUPPER "${TARGET}" upper)
    string(REGEX REPLACE "[^A-Z0-9_]" "_" token "${upper}")
    set(${OUT_VAR} "${token}" PARENT_SCOPE)
endfunction()

# id_read_catalog(CATALOG <manifest...> OUT_PREFIX <prefix>)
#
# 回传（全部 PARENT_SCOPE，<P> 为 OUT_PREFIX）：
#   <P>_COUNT                        catalog 条目数
#   <P>_NAMES                        catalog 名字列表（顺序 = 输入顺序，仅用于诊断）
#   <P>_<i>_NAME / _TARGET / _TOKEN / _MACRO / _NAMESPACE / _KIND / _VERSION / _LIFECYCLE / _DIR
#   <P>_<i>_GROUP_COUNT              需求组数
#   <P>_<i>_GROUP_<g>_CAPABILITY     该组要求的能力名
#   <P>_<i>_GROUP_<g>_CANDIDATES     该组候选名列表
#   <P>_<i>_GROUP_<g>_CAND_COUNT     该组候选数
#   <P>_<i>_GROUP_<g>_MIN_<c> / _MAX_<c>  逐候选接口修订区间
#   <P>_<i>_CANDIDATE_COUNT          候选名总数（= 全部组候选数之和）
#   <P>_<i>_PROVIDES_COUNT           provides 条数
#   <P>_<i>_PROVIDE_<p>_CAPABILITY / _REVISION / _SLOT（_SLOT 为 -1 表示无槽位）
#   <P>_TARGET_<TARGET>_INDEX        目标名 -> catalog 下标（供逐插件构建入口反查）
function(id_read_catalog)
    cmake_parse_arguments(R "" "OUT_PREFIX" "CATALOG" ${ARGN})

    if(NOT R_OUT_PREFIX OR NOT R_CATALOG)
        message(FATAL_ERROR "id_read_catalog: CATALOG 与 OUT_PREFIX 均为必填")
    endif()

    set(P "${R_OUT_PREFIX}")
    set(names "")
    set(targets "")
    set(namespaces "")
    set(macros "")
    set(index -1)

    foreach(manifest IN LISTS R_CATALOG)
        if(NOT EXISTS "${manifest}")
            message(FATAL_ERROR "catalog 清单不存在：${manifest}")
        endif()
        # 清单是本函数的输入：写进 configure 依赖后，编辑 JSON 会自动重新配置（计划书 8.4）
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")

        file(READ "${manifest}" json)

        # ---- schema：未知修订直接拒绝 ----
        string(JSON schema ERROR_VARIABLE json_error GET "${json}" schema_version)
        if(json_error)
            message(FATAL_ERROR "清单缺少 schema_version：${manifest}")
        endif()
        if(NOT schema STREQUAL "1")
            message(FATAL_ERROR "不支持的 schema_version=${schema}（只接受 1）：${manifest}")
        endif()

        # ---- 必填字段 ----
        foreach(field name target namespace kind version dependencies requirement_contracts provides lifecycle)
            string(JSON value ERROR_VARIABLE json_error GET "${json}" ${field})
            if(json_error)
                message(FATAL_ERROR "清单缺少字段 ${field}：${manifest}")
            endif()
        endforeach()

        string(JSON name      GET "${json}" name)
        string(JSON target    GET "${json}" target)
        string(JSON nspace    GET "${json}" namespace)
        string(JSON kind      GET "${json}" kind)
        string(JSON version   GET "${json}" version)
        string(JSON lifecycle GET "${json}" lifecycle)

        if(name STREQUAL "" OR target STREQUAL "" OR nspace STREQUAL "")
            message(FATAL_ERROR "清单的 name/target/namespace 不得为空：${manifest}")
        endif()
        if(NOT kind STREQUAL "module" AND NOT kind STREQUAL "interface")
            message(FATAL_ERROR "未知 kind=${kind}（只接受 module/interface）：${manifest}")
        endif()
        if(NOT lifecycle STREQUAL "standard" AND NOT lifecycle STREQUAL "none")
            message(FATAL_ERROR "未知 lifecycle=${lifecycle}（只接受 standard/none）：${manifest}")
        endif()
        if(kind STREQUAL "interface" AND NOT lifecycle STREQUAL "none")
            message(FATAL_ERROR "interface 插件必须 lifecycle=none：${manifest}")
        endif()

        # ---- 身份唯一性：名字 / 目标 / 命名空间 / 身份宏 ----
        list(FIND names "${name}" duplicate_name)
        if(duplicate_name GREATER -1)
            message(FATAL_ERROR "catalog 内重名插件：${name}（${manifest}）")
        endif()
        list(FIND targets "${target}" duplicate_target)
        if(duplicate_target GREATER -1)
            message(FATAL_ERROR "catalog 内重复 CMake 目标：${target}（${manifest}）")
        endif()
        list(FIND namespaces "${nspace}" duplicate_namespace)
        if(duplicate_namespace GREATER -1)
            message(FATAL_ERROR "catalog 内重复 C++ 命名空间：${nspace}（${manifest}）")
        endif()

        id_asm_target_token("${target}" token)
        set(macro "ID_USE_PLUGIN_${token}")
        list(FIND macros "${macro}" duplicate_macro)
        if(duplicate_macro GREATER -1)
            message(FATAL_ERROR "目标名归一化后身份宏冲突：${target} -> ${macro}（${manifest}）")
        endif()

        math(EXPR index "${index} + 1")
        list(APPEND names "${name}")
        list(APPEND targets "${target}")
        list(APPEND namespaces "${nspace}")
        list(APPEND macros "${macro}")

        get_filename_component(plugin_dir "${manifest}" DIRECTORY)

        # ---- 依赖：不等长二维数组（外层 AND、内层 OR） ----
        string(JSON group_count LENGTH "${json}" dependencies)
        set(candidate_total 0)
        math(EXPR group_last "${group_count} - 1")
        set(group_indices "")
        if(group_count GREATER 0)
            foreach(group RANGE 0 ${group_last})
                list(APPEND group_indices ${group})
            endforeach()
        endif()

        foreach(group IN LISTS group_indices)
            string(JSON candidate_count LENGTH "${json}" dependencies ${group})
            if(candidate_count LESS 1)
                message(FATAL_ERROR "依赖组 ${group} 为空内层（[[]] 非法）：${name}（${manifest}）")
            endif()
            math(EXPR candidate_last "${candidate_count} - 1")
            set(candidates "")
            foreach(candidate RANGE 0 ${candidate_last})
                string(JSON leaf GET "${json}" dependencies ${group} ${candidate})
                list(FIND candidates "${leaf}" duplicate_candidate)
                if(duplicate_candidate GREATER -1)
                    message(FATAL_ERROR "依赖组 ${group} 内重复候选 ${leaf}：${name}（${manifest}）")
                endif()
                if(leaf STREQUAL name)
                    message(FATAL_ERROR "依赖组 ${group} 含自身：${name}（${manifest}）")
                endif()
                list(APPEND candidates "${leaf}")
            endforeach()
            math(EXPR candidate_total "${candidate_total} + ${candidate_count}")

            # ---- 需求契约：能力名 + 逐候选接口修订区间 ----
            string(JSON capability ERROR_VARIABLE contract_error GET "${json}" requirement_contracts ${group} capability)
            if(contract_error)
                message(FATAL_ERROR "requirement_contracts ${group} 缺少 capability：${name}（${manifest}）")
            endif()
            string(JSON revision_count LENGTH "${json}" requirement_contracts ${group} api_revisions)
            if(NOT revision_count EQUAL candidate_count)
                message(FATAL_ERROR "需求组 ${group} 的 api_revisions 条数 ${revision_count} 与候选数 ${candidate_count} 不一致：${name}（${manifest}）")
            endif()

            set(revision_names "")
            math(EXPR revision_last "${revision_count} - 1")
            set(revision_indices "")
            if(revision_count GREATER 0)
                foreach(revision RANGE 0 ${revision_last})
                    list(APPEND revision_indices ${revision})
                endforeach()
            endif()
            foreach(revision IN LISTS revision_indices)
                string(JSON revision_name MEMBER "${json}" requirement_contracts ${group} api_revisions ${revision})
                string(JSON min_revision GET "${json}" requirement_contracts ${group} api_revisions "${revision_name}" min)
                string(JSON max_revision GET "${json}" requirement_contracts ${group} api_revisions "${revision_name}" max)
                list(APPEND revision_names "${revision_name}")
            endforeach()
            # 逐候选修订记录必须与候选名集合完全一致（不多不少、不重复）
            set(sorted_candidates "${candidates}")
            list(SORT sorted_candidates)
            set(sorted_revisions "${revision_names}")
            list(SORT sorted_revisions)
            if(NOT sorted_candidates STREQUAL sorted_revisions)
                message(FATAL_ERROR "需求组 ${group} 的 api_revisions 键集合与候选名集合不一致：${name}（${manifest}）\n  候选=${sorted_candidates}\n  修订=${sorted_revisions}")
            endif()

            # 修订按候选顺序落变量，生成器按候选下标读取
            foreach(candidate RANGE 0 ${candidate_last})
                list(GET candidates ${candidate} candidate_name)
                string(JSON min_revision GET "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" min)
                string(JSON max_revision GET "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" max)
                if(min_revision LESS 1)
                    message(FATAL_ERROR "需求组 ${group} 候选 ${candidate_name} 的 min 修订必须 >= 1：${manifest}")
                endif()
                if(max_revision LESS min_revision)
                    message(FATAL_ERROR "需求组 ${group} 候选 ${candidate_name} 的修订区间非法（max < min）：${manifest}")
                endif()
                set(${P}_${index}_GROUP_${group}_MIN_${candidate} "${min_revision}" PARENT_SCOPE)
                set(${P}_${index}_GROUP_${group}_MAX_${candidate} "${max_revision}" PARENT_SCOPE)
            endforeach()

            set(${P}_${index}_GROUP_${group}_CAPABILITY "${capability}" PARENT_SCOPE)
            set(${P}_${index}_GROUP_${group}_CANDIDATES  "${candidates}" PARENT_SCOPE)
            set(${P}_${index}_GROUP_${group}_CAND_COUNT  "${candidate_count}" PARENT_SCOPE)
        endforeach()

        # ---- provides：能力、接口修订、（可选）服务槽位 ----
        # 允许空 provides（叶子插件没有任何对外能力），与 C++ 侧 PluginMetadata::provides_count 的语义一致
        string(JSON provides_count LENGTH "${json}" provides)
        math(EXPR provides_last "${provides_count} - 1")
        set(provides_indices "")
        if(provides_count GREATER 0)
            foreach(provide RANGE 0 ${provides_last})
                list(APPEND provides_indices ${provide})
            endforeach()
        endif()
        foreach(provide IN LISTS provides_indices)
            string(JSON capability GET "${json}" provides ${provide} capability)
            string(JSON api_revision GET "${json}" provides ${provide} api_revision)
            if(capability STREQUAL "" OR api_revision LESS 1)
                message(FATAL_ERROR "provides ${provide} 的 capability/api_revision 非法：${name}（${manifest}）")
            endif()
            string(JSON slot ERROR_VARIABLE slot_error GET "${json}" provides ${provide} slot)
            if(slot_error)
                set(slot "-1")
            endif()
            if(slot LESS -1 OR slot GREATER_EQUAL 64)
                message(FATAL_ERROR "provides ${provide} 的 slot=${slot} 越界（合法 0..63，或无槽位）：${name}（${manifest}）")
            endif()
            if(kind STREQUAL "interface" AND NOT slot EQUAL -1)
                message(FATAL_ERROR "interface 插件不得声明服务槽位：${name}（${manifest}）")
            endif()
            if(slot GREATER -1 AND lifecycle STREQUAL "none")
                message(FATAL_ERROR "lifecycle=none 的插件不得声明服务槽位（没有 on_register 可发布）：${name}（${manifest}）")
            endif()
            set(${P}_${index}_PROVIDE_${provide}_CAPABILITY "${capability}"   PARENT_SCOPE)
            set(${P}_${index}_PROVIDE_${provide}_REVISION   "${api_revision}" PARENT_SCOPE)
            set(${P}_${index}_PROVIDE_${provide}_SLOT       "${slot}"         PARENT_SCOPE)
        endforeach()

        set(${P}_${index}_NAME           "${name}"      PARENT_SCOPE)
        set(${P}_${index}_TARGET         "${target}"    PARENT_SCOPE)
        set(${P}_${index}_TOKEN          "${token}"     PARENT_SCOPE)
        set(${P}_${index}_MACRO          "${macro}"     PARENT_SCOPE)
        set(${P}_${index}_NAMESPACE      "${nspace}"    PARENT_SCOPE)
        set(${P}_${index}_KIND           "${kind}"      PARENT_SCOPE)
        set(${P}_${index}_VERSION        "${version}"   PARENT_SCOPE)
        set(${P}_${index}_LIFECYCLE      "${lifecycle}" PARENT_SCOPE)
        set(${P}_${index}_DIR            "${plugin_dir}" PARENT_SCOPE)
        set(${P}_${index}_GROUP_COUNT    "${group_count}" PARENT_SCOPE)
        set(${P}_${index}_CANDIDATE_COUNT "${candidate_total}" PARENT_SCOPE)
        set(${P}_${index}_PROVIDES_COUNT "${provides_count}" PARENT_SCOPE)

        # 逐插件构建入口用它反查自己的元数据（插件 CMakeLists 只写目标名，不重复名字/依赖）
        set(${P}_TARGET_${target}_INDEX "${index}" PARENT_SCOPE)
    endforeach()

    list(LENGTH names catalog_count)
    if(catalog_count GREATER 256)
        message(FATAL_ERROR "catalog 条目数 ${catalog_count} 超过上限 256")
    endif()

    set(${P}_COUNT "${catalog_count}" PARENT_SCOPE)
    set(${P}_NAMES "${names}" PARENT_SCOPE)
endfunction()
