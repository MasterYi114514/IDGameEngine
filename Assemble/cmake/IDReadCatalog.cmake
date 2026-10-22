# 读取插件清单并做 schema 预检（计划书 8.1 / 8.2 第 2 步、第 7 节格式与身份校验）
#
# 职责：
#   1. 按显式路径读取每份 plugin.json，缺文件/缺字段/未知 schema 直接拒绝；
#   2. 校验名字、CMake 目标、C++ 命名空间的唯一性，以及 target -> ID_USE_PLUGIN_<TARGET_ID>
#      归一化后的冲突（两个目标映射到同一个身份宏必须拒绝）；
#   3. 把每份清单整理成“扁平变量表”回传，供选profile解析与生成器使用。
#
# schema 版本语义（docs/idlog-math-d0-freeze.md 的 D0-01）：
#   - schema_version=1（旧清单）：dependencies 是二维数组，每组必须恰好命中 1 个；
#     契约条目只有 {"min","max"}，规范化时按组能力补齐 capability 并标记 legacy_unique，判定不变；
#   - schema_version=2：dependencies 允许字符串项与数组项混写（字符串 = 单项必需，数组 = 组内 OR、
#     至少命中 1 个、允许同时命中多个）；契约条目逐候选携带 capability，组级 capability 只是契约标签，
#     不作为匹配真源；逐候选 capability 必须与提供者自己的 provides 一致；
#   - 两种 schema 都只有“外层 AND、内层 OR”两层；未知 schema_version 一律拒绝，不做“尽力解析”。
#
# 诊断形态：message(FATAL_ERROR "<类别>：…")；类别名与 skills/id-kernel §5.3 的 schema2 类别表一致。
#   值类型错（字符串/对象/数组错位、必需子字段缺失）-> ManifestTypeMismatch
#   语义错 -> NestedRequirementGroup / EmptyRequirementGroup / DuplicateCandidate / SelfDependency /
#             UnknownCandidate / ContractRequirementMismatch / ContractCandidateMismatch /
#             InvalidRevision / CandidateCapabilityMismatch / SlotConflict
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
#   <P>_<i>_SCHEMA                   该清单的 schema_version（1 或 2）
#   <P>_<i>_GROUP_COUNT              需求组数
#   <P>_<i>_GROUP_<g>_CAPABILITY     该组的契约标签（schema2 下不参与匹配）
#   <P>_<i>_GROUP_<g>_CANDIDATES     该组候选名列表
#   <P>_<i>_GROUP_<g>_CAND_COUNT     该组候选数
#   <P>_<i>_GROUP_<g>_LEGACY_UNIQUE  1 = schema1 旧语义（组内恰好命中 1 个）；0 = schema2（允许多命中）
#   <P>_<i>_GROUP_<g>_CAP_<c>        逐候选契约能力（匹配真源；schema1 按组能力补齐）
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

    # ---- 预扫描：先收齐 catalog 的插件名，供 UnknownCandidate 判定 --------------
    # 只读 name 且容错：缺 name 的清单留给主循环按既有顺序与文案报错，这里不抢先失败。
    set(catalog_names "")
    foreach(manifest IN LISTS R_CATALOG)
        if(EXISTS "${manifest}")
            file(READ "${manifest}" prescan_json)
            string(JSON prescan_name ERROR_VARIABLE prescan_error GET "${prescan_json}" name)
            if(NOT prescan_error)
                list(APPEND catalog_names "${prescan_name}")
            endif()
        endif()
    endforeach()

    # 第二遍校验的输入：依赖组引用（未知候选 / 逐候选能力）与全部 provides 记录
    set(reference_records "")
    set(provision_records "")

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
        if(schema STREQUAL "1")
            set(manifest_schema 1)
        elseif(schema STREQUAL "2")
            set(manifest_schema 2)
        else()
            message(FATAL_ERROR "不支持的 schema_version=${schema}（只接受 1 或 2）：${manifest}")
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

        # ---- 顶层值类型：字符串/数组错位是类型错，不是“缺字段” ----
        foreach(text_field name target namespace kind lifecycle)
            string(JSON text_type TYPE "${json}" ${text_field})
            if(NOT text_type STREQUAL "STRING")
                message(FATAL_ERROR "ManifestTypeMismatch：清单字段 ${text_field} 必须是字符串（实际 ${text_type}）：${manifest}")
            endif()
        endforeach()
        string(JSON version_type TYPE "${json}" version)
        if(NOT version_type STREQUAL "NUMBER")
            message(FATAL_ERROR "ManifestTypeMismatch：清单字段 version 必须是整数（实际 ${version_type}）：${manifest}")
        endif()
        foreach(array_field dependencies requirement_contracts provides)
            string(JSON array_type TYPE "${json}" ${array_field})
            if(NOT array_type STREQUAL "ARRAY")
                message(FATAL_ERROR "ManifestTypeMismatch：清单字段 ${array_field} 必须是数组（实际 ${array_type}）：${manifest}")
            endif()
        endforeach()

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

        # ---- 依赖：只有两层（外层 AND、内层 OR） ---------------------------------
        # schema1：每项都是候选数组，组内必须恰好命中 1 个；
        # schema2：字符串项 = 单项必需，数组项 = 组内 OR（允许同时命中多个），两者可混写。
        string(JSON group_count LENGTH "${json}" dependencies)
        set(candidate_total 0)
        math(EXPR group_last "${group_count} - 1")
        set(group_indices "")
        if(group_count GREATER 0)
            foreach(group RANGE 0 ${group_last})
                list(APPEND group_indices ${group})
            endforeach()
        endif()

        # requirement_contracts 与 dependencies 逐组对应：条数必须相同
        string(JSON contract_count LENGTH "${json}" requirement_contracts)
        if(NOT contract_count EQUAL group_count)
            message(FATAL_ERROR "ContractRequirementMismatch：需求组数 ${group_count} 与 requirement_contracts 条数 ${contract_count} 不一致（逐组对应）：${name}（${manifest}）")
        endif()

        foreach(group IN LISTS group_indices)
            string(JSON group_type TYPE "${json}" dependencies ${group})

            # 组内候选：先按项类型取原始叶子，再统一做名字级校验
            set(leaves "")
            if(group_type STREQUAL "STRING")
                if(manifest_schema EQUAL 1)
                    message(FATAL_ERROR "ManifestTypeMismatch：schema1 的 dependencies 每项必须是候选数组；依赖组 ${group} 是字符串项，字符串/数组混写请改用 schema_version=2：${name}（${manifest}）")
                endif()
                string(JSON leaf GET "${json}" dependencies ${group})
                if(leaf STREQUAL "")
                    message(FATAL_ERROR "ManifestTypeMismatch：依赖组 ${group} 的候选名为空：${name}（${manifest}）")
                endif()
                list(APPEND leaves "${leaf}")
            elseif(group_type STREQUAL "ARRAY")
                string(JSON leaf_count LENGTH "${json}" dependencies ${group})
                if(leaf_count LESS 1)
                    message(FATAL_ERROR "EmptyRequirementGroup：依赖组 ${group} 是空组（[] 不是合法需求；没有上游就整组去掉）：${name}（${manifest}）")
                endif()
                math(EXPR leaf_last "${leaf_count} - 1")
                foreach(leaf_index RANGE 0 ${leaf_last})
                    string(JSON leaf_type TYPE "${json}" dependencies ${group} ${leaf_index})
                    if(leaf_type STREQUAL "ARRAY")
                        message(FATAL_ERROR "NestedRequirementGroup：依赖组 ${group} 的第 ${leaf_index} 项仍是数组；依赖只有外层 AND、内层 OR 两层：${name}（${manifest}）")
                    endif()
                    if(NOT leaf_type STREQUAL "STRING")
                        message(FATAL_ERROR "ManifestTypeMismatch：依赖组 ${group} 的第 ${leaf_index} 项类型 ${leaf_type} 非法（只能是候选名字符串）：${name}（${manifest}）")
                    endif()
                    string(JSON leaf GET "${json}" dependencies ${group} ${leaf_index})
                    if(leaf STREQUAL "")
                        message(FATAL_ERROR "ManifestTypeMismatch：依赖组 ${group} 的第 ${leaf_index} 项候选名为空：${name}（${manifest}）")
                    endif()
                    list(APPEND leaves "${leaf}")
                endforeach()
            else()
                message(FATAL_ERROR "ManifestTypeMismatch：依赖组 ${group} 类型 ${group_type} 非法（只能是候选名字符串或候选数组）：${name}（${manifest}）")
            endif()

            # 空候选名在读叶子时就已拒绝：CMake 列表会丢弃空元素，留在这里处理会被静默漏掉
            set(candidates "")
            foreach(leaf IN LISTS leaves)
                list(FIND candidates "${leaf}" duplicate_candidate)
                if(duplicate_candidate GREATER -1)
                    message(FATAL_ERROR "DuplicateCandidate：依赖组 ${group} 内重复候选 ${leaf}：${name}（${manifest}）")
                endif()
                if(leaf STREQUAL name)
                    message(FATAL_ERROR "SelfDependency：依赖组 ${group} 含自身 ${name}：${manifest}")
                endif()
                list(APPEND candidates "${leaf}")
            endforeach()
            list(LENGTH candidates candidate_count)
            math(EXPR candidate_total "${candidate_total} + ${candidate_count}")

            # ---- 需求契约：逐候选能力 + 接口修订区间 ------------------------------
            string(JSON capability ERROR_VARIABLE contract_error GET "${json}" requirement_contracts ${group} capability)
            if(contract_error)
                message(FATAL_ERROR "ManifestTypeMismatch：requirement_contracts ${group} 缺少 capability（组级 capability 是契约标签）：${name}（${manifest}）")
            endif()
            string(JSON capability_type TYPE "${json}" requirement_contracts ${group} capability)
            if(NOT capability_type STREQUAL "STRING" OR capability STREQUAL "")
                message(FATAL_ERROR "ManifestTypeMismatch：requirement_contracts ${group} 的 capability 必须是非空字符串：${name}（${manifest}）")
            endif()
            string(JSON revisions_type ERROR_VARIABLE revisions_type_error TYPE "${json}" requirement_contracts ${group} api_revisions)
            if(revisions_type_error OR NOT revisions_type STREQUAL "OBJECT")
                message(FATAL_ERROR "ManifestTypeMismatch：需求组 ${group} 的 api_revisions 必须是对象（候选名 -> 契约）：${name}（${manifest}）")
            endif()

            string(JSON revision_count LENGTH "${json}" requirement_contracts ${group} api_revisions)
            math(EXPR revision_last "${revision_count} - 1")
            set(revision_indices "")
            if(revision_count GREATER 0)
                foreach(revision RANGE 0 ${revision_last})
                    list(APPEND revision_indices ${revision})
                endforeach()
            endif()

            set(revision_names "")
            foreach(revision IN LISTS revision_indices)
                string(JSON revision_name MEMBER "${json}" requirement_contracts ${group} api_revisions ${revision})
                string(JSON entry_type TYPE "${json}" requirement_contracts ${group} api_revisions "${revision_name}")
                if(NOT entry_type STREQUAL "OBJECT")
                    message(FATAL_ERROR "ManifestTypeMismatch：需求组 ${group} 候选 ${revision_name} 的契约必须是对象 {capability,min,max}（实际 ${entry_type}）：${name}（${manifest}）")
                endif()
                list(APPEND revision_names "${revision_name}")
            endforeach()

            # 键集合必须与候选名集合完全一致（不多不少）：缺条目 / 多条目分别诊断
            set(missing_contracts "")
            foreach(candidate_name IN LISTS candidates)
                list(FIND revision_names "${candidate_name}" contract_position)
                if(contract_position LESS 0)
                    list(APPEND missing_contracts "${candidate_name}")
                endif()
            endforeach()
            set(extra_contracts "")
            foreach(revision_name IN LISTS revision_names)
                list(FIND candidates "${revision_name}" candidate_position)
                if(candidate_position LESS 0)
                    list(APPEND extra_contracts "${revision_name}")
                endif()
            endforeach()
            list(LENGTH missing_contracts missing_contract_count)
            list(LENGTH extra_contracts extra_contract_count)
            if(missing_contract_count GREATER 0)
                message(FATAL_ERROR "ContractRequirementMismatch：需求组 ${group} 缺少候选 ${missing_contracts} 的契约条目（键集合必须与候选集合完全一致）：${name}（${manifest}）")
            endif()
            if(extra_contract_count GREATER 0)
                message(FATAL_ERROR "ContractCandidateMismatch：需求组 ${group} 的契约条目 ${extra_contracts} 不是该组的候选（键集合必须与候选集合完全一致）：${name}（${manifest}）")
            endif()

            # schema1 沿用“组内恰好命中 1 个 + 组能力”的旧语义，标记 legacy_unique
            if(manifest_schema EQUAL 1)
                set(legacy_unique 1)
            else()
                set(legacy_unique 0)
            endif()

            set(candidate_position 0)
            foreach(candidate_name IN LISTS candidates)
                string(JSON min_revision ERROR_VARIABLE min_error GET "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" min)
                string(JSON max_revision ERROR_VARIABLE max_error GET "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" max)
                if(min_error OR max_error)
                    message(FATAL_ERROR "ManifestTypeMismatch：需求组 ${group} 候选 ${candidate_name} 的契约缺少 min/max：${name}（${manifest}）")
                endif()
                string(JSON min_type TYPE "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" min)
                string(JSON max_type TYPE "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" max)
                if(NOT min_type STREQUAL "NUMBER" OR NOT max_type STREQUAL "NUMBER")
                    message(FATAL_ERROR "ManifestTypeMismatch：需求组 ${group} 候选 ${candidate_name} 的 min/max 必须是整数：${name}（${manifest}）")
                endif()
                if(NOT min_revision MATCHES "^[0-9]+$" OR NOT max_revision MATCHES "^[0-9]+$")
                    message(FATAL_ERROR "InvalidRevision：需求组 ${group} 候选 ${candidate_name} 的修订区间必须是整数：[${min_revision}, ${max_revision}]：${name}（${manifest}）")
                endif()
                if(min_revision LESS 1)
                    message(FATAL_ERROR "InvalidRevision：需求组 ${group} 候选 ${candidate_name} 的 min 修订必须 >= 1：${name}（${manifest}）")
                endif()
                if(max_revision LESS min_revision)
                    message(FATAL_ERROR "InvalidRevision：需求组 ${group} 候选 ${candidate_name} 的修订区间非法（max < min）：${name}（${manifest}）")
                endif()

                # 逐候选契约能力：schema2 必须显式携带且是匹配真源；
                # schema1 由组能力规范化补齐（legacy_unique），若清单额外写了 capability 必须与组能力一致。
                if(manifest_schema EQUAL 2)
                    string(JSON declared ERROR_VARIABLE declared_error GET "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" capability)
                    if(declared_error)
                        message(FATAL_ERROR "ManifestTypeMismatch：schema2 的逐候选契约必须携带 capability（需求组 ${group} 候选 ${candidate_name}）：${name}（${manifest}）")
                    endif()
                    string(JSON declared_type TYPE "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" capability)
                    if(NOT declared_type STREQUAL "STRING" OR declared STREQUAL "")
                        message(FATAL_ERROR "ManifestTypeMismatch：需求组 ${group} 候选 ${candidate_name} 的 capability 必须是非空字符串：${name}（${manifest}）")
                    endif()
                else()
                    string(JSON declared ERROR_VARIABLE declared_error GET "${json}" requirement_contracts ${group} api_revisions "${candidate_name}" capability)
                    if(NOT declared_error AND NOT declared STREQUAL capability)
                        message(FATAL_ERROR "CandidateCapabilityMismatch：schema1 不能表达“不同能力的 OR”，候选 ${candidate_name} 的 capability=${declared} 必须等于组能力 ${capability}（需要逐候选能力请改用 schema_version=2）：${name}（${manifest}）")
                    endif()
                    set(declared "${capability}")
                endif()

                set(${P}_${index}_GROUP_${group}_MIN_${candidate_position} "${min_revision}" PARENT_SCOPE)
                set(${P}_${index}_GROUP_${group}_MAX_${candidate_position} "${max_revision}" PARENT_SCOPE)
                set(${P}_${index}_GROUP_${group}_CAP_${candidate_position} "${declared}"     PARENT_SCOPE)
                list(APPEND reference_records "${name}|${group}|${candidate_name}|${declared}|${manifest_schema}|${manifest}")
                math(EXPR candidate_position "${candidate_position} + 1")
            endforeach()

            set(${P}_${index}_GROUP_${group}_CAPABILITY    "${capability}"     PARENT_SCOPE)
            set(${P}_${index}_GROUP_${group}_CANDIDATES    "${candidates}"     PARENT_SCOPE)
            set(${P}_${index}_GROUP_${group}_CAND_COUNT    "${candidate_count}" PARENT_SCOPE)
            set(${P}_${index}_GROUP_${group}_LEGACY_UNIQUE "${legacy_unique}"  PARENT_SCOPE)
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
            string(JSON provide_type TYPE "${json}" provides ${provide})
            if(NOT provide_type STREQUAL "OBJECT")
                message(FATAL_ERROR "ManifestTypeMismatch：provides ${provide} 必须是对象 {capability,api_revision[,slot]}（实际 ${provide_type}）：${name}（${manifest}）")
            endif()
            string(JSON capability ERROR_VARIABLE provide_capability_error GET "${json}" provides ${provide} capability)
            string(JSON api_revision ERROR_VARIABLE provide_revision_error GET "${json}" provides ${provide} api_revision)
            if(provide_capability_error OR provide_revision_error)
                message(FATAL_ERROR "ManifestTypeMismatch：provides ${provide} 缺少 capability/api_revision：${name}（${manifest}）")
            endif()
            string(JSON capability_type TYPE "${json}" provides ${provide} capability)
            string(JSON api_revision_type TYPE "${json}" provides ${provide} api_revision)
            if(NOT capability_type STREQUAL "STRING" OR capability STREQUAL "")
                message(FATAL_ERROR "ManifestTypeMismatch：provides ${provide} 的 capability 必须是非空字符串：${name}（${manifest}）")
            endif()
            if(NOT api_revision_type STREQUAL "NUMBER" OR NOT api_revision MATCHES "^[0-9]+$" OR api_revision LESS 1)
                message(FATAL_ERROR "InvalidRevision：provides ${provide} 的 api_revision 必须是 >= 1 的整数（本实现自己的接口修订）：${name}（${manifest}）")
            endif()
            string(JSON slot ERROR_VARIABLE slot_error GET "${json}" provides ${provide} slot)
            if(slot_error)
                set(slot "-1")
            else()
                string(JSON slot_type TYPE "${json}" provides ${provide} slot)
                if(NOT slot_type STREQUAL "NUMBER" OR NOT slot MATCHES "^-?[0-9]+$")
                    message(FATAL_ERROR "ManifestTypeMismatch：provides ${provide} 的 slot 必须是 0..63 的整数（无槽位就不写该字段）：${name}（${manifest}）")
                endif()
            endif()
            if(slot LESS -1 OR slot GREATER_EQUAL 64)
                message(FATAL_ERROR "SlotConflict：provides ${provide} 的槽位 ${slot} 越界（合法 0..63，或无槽位）：${name}（${manifest}）")
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
            list(APPEND provision_records "${name}|${capability}")
        endforeach()

        set(${P}_${index}_NAME           "${name}"      PARENT_SCOPE)
        set(${P}_${index}_TARGET         "${target}"    PARENT_SCOPE)
        set(${P}_${index}_TOKEN          "${token}"     PARENT_SCOPE)
        set(${P}_${index}_MACRO          "${macro}"     PARENT_SCOPE)
        set(${P}_${index}_NAMESPACE      "${nspace}"    PARENT_SCOPE)
        set(${P}_${index}_KIND           "${kind}"      PARENT_SCOPE)
        set(${P}_${index}_VERSION        "${version}"   PARENT_SCOPE)
        set(${P}_${index}_LIFECYCLE      "${lifecycle}" PARENT_SCOPE)
        set(${P}_${index}_SCHEMA         "${manifest_schema}" PARENT_SCOPE)
        set(${P}_${index}_DIR            "${plugin_dir}" PARENT_SCOPE)
        set(${P}_${index}_GROUP_COUNT    "${group_count}" PARENT_SCOPE)
        set(${P}_${index}_CANDIDATE_COUNT "${candidate_total}" PARENT_SCOPE)
        set(${P}_${index}_PROVIDES_COUNT "${provides_count}" PARENT_SCOPE)

        # 逐插件构建入口用它反查自己的元数据（插件 CMakeLists 只写目标名，不重复名字/依赖）
        set(${P}_TARGET_${target}_INDEX "${index}" PARENT_SCOPE)
    endforeach()

    # ---- 跨清单校验：未知候选 + 逐候选能力必须被提供者自己声明 -------------------
    # 放在主循环之后，是因为这两项都要看到完整 catalog：候选可能在别的清单里，
    # 也可能在本清单之后才被读到。catalog 条目即使不被选中也要通过这两项校验。
    foreach(record IN LISTS reference_records)
        string(REPLACE "|" ";" parts "${record}")
        list(GET parts 0 dependent_name)
        list(GET parts 1 group_index)
        list(GET parts 2 candidate_name)
        list(GET parts 3 declared_capability)
        list(GET parts 4 record_schema)
        list(GET parts 5 record_manifest)

        list(FIND catalog_names "${candidate_name}" provider_position)
        if(provider_position LESS 0)
            message(FATAL_ERROR "UnknownCandidate：${dependent_name} 的需求组 ${group_index} 引用了 catalog 之外的候选 ${candidate_name}（先把它的 plugin.json 登记到 catalog.cmake）：${record_manifest}")
        endif()

        if(record_schema EQUAL 2)
            # schema2：逐候选 capability 是匹配真源，必须与提供者 provides 里的能力逐字相同
            list(FIND provision_records "${candidate_name}|${declared_capability}" provided_position)
            if(provided_position LESS 0)
                message(FATAL_ERROR "CandidateCapabilityMismatch：${candidate_name} 没有提供它在 ${dependent_name} 的需求组 ${group_index} 中声明的能力 ${declared_capability}（schema2 的逐候选 capability 是匹配真源）：${record_manifest}")
            endif()
        endif()
    endforeach()

    list(LENGTH names catalog_count)
    if(catalog_count GREATER 256)
        message(FATAL_ERROR "catalog 条目数 ${catalog_count} 超过上限 256")
    endif()

    set(${P}_COUNT "${catalog_count}" PARENT_SCOPE)
    set(${P}_NAMES "${names}" PARENT_SCOPE)
endfunction()
