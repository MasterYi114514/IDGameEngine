# 从 catalog + profile 派生本产品的真实组装关系（计划书 6 / 8.2 第 1/3 步、第 7 节）
#
# 职责：
#   1. 读 profile 的显式选中集合：不自动补依赖、不按候选顺序决策、不按版本“择优”；
#   2. 在 Selected 内为每个需求求命中集合 `matches = candidates ∩ selected`：
#      schema1（legacy_unique）仍要求组内恰好 1 个 —— 0 个 -> MissingProvider、>= 2 -> AmbiguousProvider；
#      schema2 允许 0 / 1 / N（N 不超过组容量），N > 1 不是错误，逐条独立落地；
#   3. 每个命中候选都必须自己通过“确实提供它声明的那条能力 + 修订落在该候选区间”两项校验，
#      任一命中候选校验失败即整组失败（不跳过坏的、不留好的）；
#   4. 检查能力与槽位的产品级唯一性（跨组也查）；
#   5. 派生真实 PUBLIC 链接边（按 (上游, 下游) 去重）、逐命中 binding 记录、逐 consumer × 逐需求组的
#      matched 集合、槽位归属与目标身份证据，交给生成器与 C++ 侧复核。
#
# 明确不做的事：
#   - 不做拓扑排序（最终顺序由 C++ 侧 sort_and_validate 产出）；
#   - 不向插件注入与自身无关的身份：身份宏只作为对应上游 target 的 PUBLIC usage requirement 传播；
#   - 不引入 preferred / 唯一 binding / 顺序择优 / 按版本挑最新：命中集合由选中集合唯一决定。
include_guard()

# 64 位无符号 FNV-1a（与 ID::hash_fnv_1a64 / PluginCompileTime 的口径一致）。
#
# CMake 的 math(EXPR) 只有 64 位有符号整数，直接乘会溢成负数，因此把哈希拆成 4 个 16 位 limb
# （base 65536）做手写进位：所有中间量 <= 5 * 65535^2 ≈ 2.1e10，始终在安全范围内。
# 该实现的正确性由 C++ 侧证据校验兜底：任何一个字节算错都会让
# EvidenceIdentityMismatch / RuntimeBindingMismatch 在编译期触发。
function(id_asm_fnv1a64 OUT_VAR TEXT)
    # FNV-1a 64：offset basis 0xCBF29CE484222325，prime 0x100000001B3
    set(h0 0x2325)
    set(h1 0x8422)
    set(h2 0x9CE4)
    set(h3 0xCBF2)
    set(p0 0x01B3)
    set(p1 0x0000)
    set(p2 0x0100)

    string(LENGTH "${TEXT}" length)
    set(index 0)
    while(index LESS length)
        string(SUBSTRING "${TEXT}" ${index} 1 text_char)
        string(HEX "${text_char}" text_hex)
        math(EXPR code "0x${text_hex}")

        # h ^= byte（只影响最低 limb）
        math(EXPR h0 "${h0} ^ ${code}")

        # h *= prime，按 limb 展开（p1 = 0，因此相关项省略）
        math(EXPR t0 "${h0} * ${p0}")
        math(EXPR t1 "${h1} * ${p0}")
        math(EXPR t2 "${h0} * ${p2} + ${h2} * ${p0}")
        math(EXPR t3 "${h1} * ${p2} + ${h3} * ${p0}")

        math(EXPR carry "${t0} / 65536")
        math(EXPR h0 "${t0} % 65536")
        math(EXPR t1 "${t1} + ${carry}")
        math(EXPR carry "${t1} / 65536")
        math(EXPR h1 "${t1} % 65536")
        math(EXPR t2 "${t2} + ${carry}")
        math(EXPR carry "${t2} / 65536")
        math(EXPR h2 "${t2} % 65536")
        math(EXPR t3 "${t3} + ${carry}")
        math(EXPR h3 "${t3} % 65536")

        math(EXPR index "${index} + 1")
    endwhile()

    # 按 4 个 16 进制位拼回字面量，交给 C++ 侧当无符号常量使用
    set(digits "0;1;2;3;4;5;6;7;8;9;a;b;c;d;e;f")
    set(hex "")
    foreach(limb IN ITEMS ${h3} ${h2} ${h1} ${h0})
        set(piece "")
        set(value ${limb})
        foreach(nibble_index 1 2 3 4)
            math(EXPR nibble "${value} % 16")
            list(GET digits ${nibble} digit)
            set(piece "${digit}${piece}")
            math(EXPR value "${value} / 16")
        endforeach()
        set(hex "${hex}${piece}")
    endforeach()

    set(${OUT_VAR} "0x${hex}ull" PARENT_SCOPE)
endfunction()

# 把目标名归一化成的身份宏（不含 =1）回传
function(id_asm_identity_macro TARGET OUT_VAR)
    id_asm_target_token("${TARGET}" token)
    set(${OUT_VAR} "ID_USE_PLUGIN_${token}" PARENT_SCOPE)
endfunction()

# id_resolve_selection(PROFILE <profile.json> CATALOG_PREFIX <P> OUT_PREFIX <Q>)
#
# 回传（Q = OUT_PREFIX）：
#   Q_ASSEMBLY_NAME / Q_PROFILE_PATH
#   Q_SELECTED_COUNT / Q_SELECTED_INDICES / Q_SELECTED_NAMES / Q_SELECTED_TARGETS
#   Q_BINDING_COUNT / Q_BINDINGS          "下游名|命中provider名|需求组下标"，每条 matched 记录一条
#                                         （schema2 组内多命中时同一组会出现多条，Q_BINDING_COUNT 因此是
#                                          matched 记录总数，而不是需求组数）
#   Q_EDGE_COUNT    / Q_EDGES             "上游名|下游名"（已按 (上游,下游) 去重：多组指向同一上游只连一次）
#   Q_WIRING                              "下游target|上游target"（真实 PUBLIC 链接边，与 Q_EDGES 同序同数）
#   Q_MATCHED_GROUPS                      "选中下标|需求组|命中条数|命中provider token（逗号分隔）"
#                                         逐 consumer × 逐需求组的 matched 集合（availability 生成物的输入）
#   Q_SLOT_COUNT    / Q_SLOTS             "owner名|能力名|槽位"
#   Q_PROVISION_COUNT / Q_RUNTIME         "插件名|能力名|plugin_id|接口修订"
#   Q_SELECTED_EVIDENCE                   "插件名|target|身份宏|plugin_id"
#   Q_EVIDENCE_LINES                      人类可读的构建证据（逐需求命中 / matched-set / 边 / 槽 / 目标）
function(id_resolve_selection)
    cmake_parse_arguments(R "" "PROFILE;CATALOG_PREFIX;OUT_PREFIX" "" ${ARGN})

    if(NOT R_PROFILE OR NOT R_CATALOG_PREFIX OR NOT R_OUT_PREFIX)
        message(FATAL_ERROR "id_resolve_selection: PROFILE / CATALOG_PREFIX / OUT_PREFIX 均为必填")
    endif()
    if(NOT EXISTS "${R_PROFILE}")
        message(FATAL_ERROR "profile 不存在：${R_PROFILE}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${R_PROFILE}")

    set(C "${R_CATALOG_PREFIX}")
    set(Q "${R_OUT_PREFIX}")

    # ---------------- profile ----------------
    file(READ "${R_PROFILE}" profile)
    string(JSON profile_schema ERROR_VARIABLE profile_error GET "${profile}" schema_version)
    if(profile_error)
        message(FATAL_ERROR "profile 缺少 schema_version：${R_PROFILE}")
    endif()
    if(NOT profile_schema STREQUAL "1")
        message(FATAL_ERROR "不支持的 profile schema_version=${profile_schema}（只接受 1）：${R_PROFILE}")
    endif()
    string(JSON assembly_name ERROR_VARIABLE name_error GET "${profile}" assembly_name)
    if(name_error OR assembly_name STREQUAL "")
        message(FATAL_ERROR "profile 缺少 assembly_name：${R_PROFILE}")
    endif()

    string(JSON enabled_count LENGTH "${profile}" enabled_plugins)
    if(enabled_count GREATER 128)
        message(FATAL_ERROR "profile 启用 ${enabled_count} 个插件，超过 active 上限 128")
    endif()

    set(selected_indices "")
    set(selected_names "")
    set(selected_targets "")
    set(enabled_indices "")
    if(enabled_count GREATER 0)
        math(EXPR enabled_last "${enabled_count} - 1")
        foreach(item RANGE 0 ${enabled_last})
            list(APPEND enabled_indices ${item})
        endforeach()
    endif()

    foreach(item IN LISTS enabled_indices)
        string(JSON enabled_name GET "${profile}" enabled_plugins ${item})
        list(FIND ${C}_NAMES "${enabled_name}" catalog_index)
        if(catalog_index LESS 0)
            message(FATAL_ERROR "profile 选中了 catalog 之外的名字：${enabled_name}")
        endif()
        list(FIND selected_indices "${catalog_index}" already)
        if(already GREATER -1)
            message(FATAL_ERROR "profile 重复启用：${enabled_name}")
        endif()
        list(APPEND selected_indices "${catalog_index}")
        list(APPEND selected_names   "${${C}_${catalog_index}_NAME}")
        list(APPEND selected_targets "${${C}_${catalog_index}_TARGET}")
    endforeach()

    # ---------------- 逐需求命中集合（0 / 1 / N） ----------------
    set(bindings "")
    set(edges "")
    set(wiring "")
    set(evidence_lines "")
    set(matched_groups "")

    # 能力与槽位的产品级唯一性（即使没有下游使用也要检查，计划书 6.2）
    set(capability_owners "")
    set(slot_owners "")

    foreach(selected_index IN LISTS selected_indices)
        set(selected_name   "${${C}_${selected_index}_NAME}")
        set(selected_target "${${C}_${selected_index}_TARGET}")

        # 1. provides：能力唯一 + 槽位唯一
        set(provides_count "${${C}_${selected_index}_PROVIDES_COUNT}")
        math(EXPR provides_last "${provides_count} - 1")
        set(provide_indices "")
        if(provides_count GREATER 0)
            foreach(provide RANGE 0 ${provides_last})
                list(APPEND provide_indices ${provide})
            endforeach()
        endif()
        foreach(provide IN LISTS provide_indices)
            set(capability "${${C}_${selected_index}_PROVIDE_${provide}_CAPABILITY}")
            set(slot       "${${C}_${selected_index}_PROVIDE_${provide}_SLOT}")

            list(FIND capability_owners "${capability}" capability_owner)
            if(capability_owner GREATER -1)
                message(FATAL_ERROR "DuplicateCapability：能力 ${capability} 在选中集合内有两个提供者")
            endif()
            list(APPEND capability_owners "${capability}")

            if(NOT slot EQUAL -1)
                if(slot LESS 0 OR slot GREATER_EQUAL 64)
                    message(FATAL_ERROR "SlotConflict：${selected_name} 的槽位 ${slot} 越界")
                endif()
                foreach(record IN LISTS slot_owners)
                    string(REPLACE "=" ";" slot_parts "${record}")
                    list(GET slot_parts 0 recorded_slot)
                    if(recorded_slot EQUAL slot)
                        message(FATAL_ERROR "SlotConflict：槽位 ${slot} 被 ${selected_name} 与其它选中插件同时占用")
                    endif()
                endforeach()
                list(APPEND slot_owners "${slot}=${selected_name}")
            endif()
        endforeach()

        # 2. 逐需求：在 Selected 内求命中集合（schema1 组内恰好 1 个，schema2 允许 0/1/N）
        set(group_count "${${C}_${selected_index}_GROUP_COUNT}")
        math(EXPR group_last "${group_count} - 1")
        set(group_indices "")
        if(group_count GREATER 0)
            foreach(group RANGE 0 ${group_last})
                list(APPEND group_indices ${group})
            endforeach()
        endif()

        foreach(group IN LISTS group_indices)
            set(candidates    "${${C}_${selected_index}_GROUP_${group}_CANDIDATES}")
            set(capability    "${${C}_${selected_index}_GROUP_${group}_CAPABILITY}")
            set(legacy_unique "${${C}_${selected_index}_GROUP_${group}_LEGACY_UNIQUE}")

            # matches = candidates ∩ selected。记录形如 "<catalog 下标>|<候选在组内的位置>|<候选名>"，
            # 按 catalog 下标升序排列：matched 记录顺序因此只由 catalog 决定，
            # 候选书写顺序、profile 书写顺序都不参与（canonical 证据段与 C++ 侧指纹逐字节稳定）。
            set(matched_records "")
            set(candidate_position 0)
            foreach(candidate IN LISTS candidates)
                list(FIND selected_names "${candidate}" selected_position)
                if(selected_position GREATER -1)
                    list(FIND ${C}_NAMES "${candidate}" candidate_catalog_index)
                    list(APPEND matched_records "${candidate_catalog_index}|${candidate_position}|${candidate}")
                endif()
                math(EXPR candidate_position "${candidate_position} + 1")
            endforeach()
            list(LENGTH matched_records match_count)
            if(match_count GREATER 1)
                list(SORT matched_records COMPARE NATURAL)
            endif()

            # 预算先于语义判定：组容量 = kMaxCandidates = 16（超出后 matched 表也不再有位置）
            if(match_count GREATER 16)
                message(FATAL_ERROR "TooManyCandidates：${selected_name} 的需求组 ${group}（契约标签 ${capability}）命中 ${match_count} 个候选，超过组容量 16；不挑第一个，也不挑新版本")
            endif()
            if(match_count EQUAL 0)
                message(FATAL_ERROR "MissingProvider：${selected_name} 的需求组 ${group}（契约标签 ${capability}）在选中集合内没有候选；不自动启用缺失的依赖")
            endif()
            # schema1 仍要求组内恰好一个（legacy_unique = 1）：多命中不因 schema2 落地而被放宽
            if(legacy_unique EQUAL 1 AND match_count GREATER 1)
                message(FATAL_ERROR "AmbiguousProvider：${selected_name} 的需求组 ${group}（契约标签 ${capability}）命中 ${match_count} 个候选；schema1 要求组内恰好 1 个，不挑第一个也不挑新版本（需要组内多命中请把清单迁到 schema_version=2）")
            endif()

            set(group_tokens "")
            foreach(record IN LISTS matched_records)
                string(REPLACE "|" ";" record_parts "${record}")
                list(GET record_parts 0 provider_index)
                list(GET record_parts 1 candidate_offset)
                list(GET record_parts 2 provider)

                set(provider_target "${${C}_${provider_index}_TARGET}")
                set(provider_token  "${${C}_${provider_index}_TOKEN}")

                # 3. 匹配真源是候选自己声明的那条能力（schema1 已在解析时规范化为组能力），
                #    组级 capability 只是契约标签，这里不再拿它当匹配真源。
                set(declared_capability "${${C}_${selected_index}_GROUP_${group}_CAP_${candidate_offset}}")
                set(min_revision        "${${C}_${selected_index}_GROUP_${group}_MIN_${candidate_offset}}")
                set(max_revision        "${${C}_${selected_index}_GROUP_${group}_MAX_${candidate_offset}}")
                if(declared_capability STREQUAL "" OR min_revision LESS 0 OR max_revision LESS 0)
                    message(FATAL_ERROR "内部错误：${selected_name} 的需求组 ${group} 缺少 ${provider} 的逐候选契约（capability / [min,max]）")
                endif()

                set(provided_revision "")
                set(provider_provides_count "${${C}_${provider_index}_PROVIDES_COUNT}")
                math(EXPR provider_provides_last "${provider_provides_count} - 1")
                set(provider_provides_indices "")
                if(provider_provides_count GREATER 0)
                    foreach(provide RANGE 0 ${provider_provides_last})
                        list(APPEND provider_provides_indices ${provide})
                    endforeach()
                endif()
                foreach(provide IN LISTS provider_provides_indices)
                    if("${${C}_${provider_index}_PROVIDE_${provide}_CAPABILITY}" STREQUAL "${declared_capability}")
                        set(provided_revision "${${C}_${provider_index}_PROVIDE_${provide}_REVISION}")
                    endif()
                endforeach()
                if(provided_revision STREQUAL "")
                    message(FATAL_ERROR "CandidateCapabilityMismatch：${provider} 没有提供能力 ${declared_capability}（${selected_name} 的需求组 ${group}）；组级契约标签 ${capability} 不作为匹配真源")
                endif()
                if(provided_revision LESS min_revision OR provided_revision GREATER max_revision)
                    message(FATAL_ERROR "RevisionMismatch：${provider} 提供 ${declared_capability} 的接口修订 ${provided_revision} 不在 ${selected_name} 要求的 [${min_revision}, ${max_revision}] 内；不改选其它命中，也不跳过本命中")
                endif()

                # 每条命中各保留一条 binding：多条命中同一个 provider（跨组或同组）都不合并
                list(APPEND bindings "${selected_name}|${provider}|${group}")
                list(APPEND evidence_lines "provider  ${selected_name} -> ${provider}（需求组 ${group}，能力 ${declared_capability}，修订 ${provided_revision}）")
                list(APPEND group_tokens "${provider_token}")

                # 4. 真实链接边按 (上游, 下游) 去重：只保留一条 PUBLIC 链接与一条身份宏 usage
                set(edge_record "${provider}|${selected_name}")
                list(FIND edges "${edge_record}" duplicate_edge)
                if(duplicate_edge LESS 0)
                    list(APPEND edges "${edge_record}")
                    list(APPEND wiring "${selected_target}|${provider_target}")
                    list(APPEND evidence_lines "edge      ${provider} -> ${selected_name}（PUBLIC 链接 + 身份宏 ID_USE_PLUGIN_${provider_token}）")
                endif()
            endforeach()

            # 逐 consumer × 逐需求组的 matched 集合：availability 生成物的唯一输入
            string(REPLACE ";" "," group_token_text "${group_tokens}")
            list(APPEND matched_groups "${selected_index}|${group}|${match_count}|${group_token_text}")
            list(APPEND evidence_lines "matched   ${selected_name} 需求组 ${group}（契约标签 ${capability}）命中 ${match_count} 个：${group_token_text}（每 consumer × 每组的可见性宏见 ${selected_target}Matched.generated.hpp）")
        endforeach()
    endforeach()

    # ---------------- 证据：选中身份 / 槽位 / 运行期绑定元数据 ----------------
    set(selected_evidence "")
    set(slots "")
    set(runtime "")
    set(provision_count 0)

    foreach(selected_index IN LISTS selected_indices)
        set(selected_name   "${${C}_${selected_index}_NAME}")
        set(selected_target "${${C}_${selected_index}_TARGET}")
        set(selected_macro  "${${C}_${selected_index}_MACRO}")
        id_asm_fnv1a64(plugin_id "${selected_name}")
        list(APPEND selected_evidence "${selected_name}|${selected_target}|${selected_macro}|${plugin_id}")
        list(APPEND evidence_lines "selected  ${selected_name} target=${selected_target} macro=${selected_macro} id=${plugin_id}")

        set(provides_count "${${C}_${selected_index}_PROVIDES_COUNT}")
        math(EXPR provides_last "${provides_count} - 1")
        set(provide_indices "")
        if(provides_count GREATER 0)
            foreach(provide RANGE 0 ${provides_last})
                list(APPEND provide_indices ${provide})
            endforeach()
        endif()
        foreach(provide IN LISTS provide_indices)
            set(capability   "${${C}_${selected_index}_PROVIDE_${provide}_CAPABILITY}")
            set(api_revision "${${C}_${selected_index}_PROVIDE_${provide}_REVISION}")
            set(slot         "${${C}_${selected_index}_PROVIDE_${provide}_SLOT}")
            math(EXPR provision_count "${provision_count} + 1")
            list(APPEND runtime "${selected_name}|${capability}|${plugin_id}|${api_revision}")
            if(NOT slot EQUAL -1)
                list(APPEND slots "${selected_name}|${capability}|${slot}")
                list(APPEND evidence_lines "slot      ${selected_name}.${capability} -> ${slot}")
            else()
                list(APPEND evidence_lines "slot      ${selected_name}.${capability} -> 无运行期槽位")
            endif()
        endforeach()
    endforeach()

    list(LENGTH selected_indices selected_count)
    list(LENGTH bindings binding_count)
    list(LENGTH edges edge_count)
    list(LENGTH slots slot_count)

    # 计划表预算（D0-09）：matched 记录进扁平命中表（kMatchSlots = 65536），去重边进边表（kEdgeCapacity = 16256）。
    # 两者都是静态计划表的容量上界，运行期没有扩容入口，因此超额一律在 configure 期按具体类别拒绝。
    if(binding_count GREATER 65536)
        message(FATAL_ERROR "TooManyMatchedRecords：本产品 matched 记录 ${binding_count} 条，超过扁平命中表容量 65536（kGroupSlots × kMaxCandidates）")
    endif()
    if(edge_count GREATER 16256)
        message(FATAL_ERROR "TooManyEdges：本产品按 (上游, 下游) 去重后仍有 ${edge_count} 条依赖边，超过边表容量 16256（kMaxActive × (kMaxActive - 1)）")
    endif()

    set(${Q}_ASSEMBLY_NAME      "${assembly_name}"   PARENT_SCOPE)
    set(${Q}_PROFILE_PATH       "${R_PROFILE}"       PARENT_SCOPE)
    set(${Q}_SELECTED_COUNT     "${selected_count}"  PARENT_SCOPE)
    set(${Q}_SELECTED_INDICES   "${selected_indices}" PARENT_SCOPE)
    set(${Q}_SELECTED_NAMES     "${selected_names}"  PARENT_SCOPE)
    set(${Q}_SELECTED_TARGETS   "${selected_targets}" PARENT_SCOPE)
    set(${Q}_BINDING_COUNT      "${binding_count}"   PARENT_SCOPE)
    set(${Q}_BINDINGS           "${bindings}"        PARENT_SCOPE)
    set(${Q}_EDGE_COUNT         "${edge_count}"      PARENT_SCOPE)
    set(${Q}_EDGES              "${edges}"           PARENT_SCOPE)
    set(${Q}_WIRING             "${wiring}"          PARENT_SCOPE)
    set(${Q}_MATCHED_GROUPS     "${matched_groups}"  PARENT_SCOPE)
    set(${Q}_SLOT_COUNT         "${slot_count}"      PARENT_SCOPE)
    set(${Q}_SLOTS              "${slots}"           PARENT_SCOPE)
    set(${Q}_PROVISION_COUNT    "${provision_count}" PARENT_SCOPE)
    set(${Q}_RUNTIME            "${runtime}"         PARENT_SCOPE)
    set(${Q}_SELECTED_EVIDENCE  "${selected_evidence}" PARENT_SCOPE)
    set(${Q}_EVIDENCE_LINES     "${evidence_lines}"  PARENT_SCOPE)
endfunction()
