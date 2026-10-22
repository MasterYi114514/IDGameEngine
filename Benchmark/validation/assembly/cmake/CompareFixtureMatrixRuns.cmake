# 组装 fixture 矩阵 —— 四配置一致性对照（cmake -P）
#
# 职责：把**多个配置各自跑出来的** fixture 矩阵机器可读记录（out/fixture-matrix-cases.txt）
# 逐字段对照，输出一张对照表并给出"是否一致"的判定。它不重新跑任何 case，也不重新判定语义。
#
# 为什么单独一个脚本：单份报告只覆盖它自己那一格；四配置（MinGW/MSVC × Debug/Release）
# 一致性不能由任何单份报告自述（计划书 §9.7 证据清单要求逐格外部退出码 + §9.8 官方 report 唯一性）。
#
# 参数：
#   SOURCES —— 每项 `label|cases_file|meta_file|driver_exit_file|build_dir`，用分号分隔多项；
#              顺序即对照表中的列顺序。
#   OUT     —— 本脚本产出的对照报告路径（建议放在某个构建目录的
#              Benchmark/validation/assembly/out/fixture-matrix-four-config.txt）。
#
# 判定口径：
#   - 必须完全相同（不一致即失败，逐条列出待解释）：kind / expect / configure 退出码 /
#     编译状态 / 编译期标记 / 判定结果 / selected / bindings / edges / slots / provisions /
#     canonbind / canonical-sha256 / fingerprint / 类别（configure 期类别集合）；
#   - 允许不同（对照表里如实并列，不算失败）：generator / build type / config / cxx /
#     driver 外部退出码 / 编译子进程的**原始**退出码（语义由"编译状态"归一化后比较）。
#   - 四配置矩阵完整性：必须同时出现 {MinGW, MSVC} × {Debug, Release} 四格。
#
# 退出码：0 = 一致性检查通过且四格齐全；1 = 有必须解释的差异或矩阵不完整。

foreach(required_var SOURCES OUT)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "CompareFixtureMatrixRuns: 缺少参数 ${required_var}")
    endif()
endforeach()

set(problems "")
set(labels "")
set(src_index 0)

# 逐 case 记录的字段顺序（与 RunFixtureMatrix.cmake 写出的顺序严格一致）
set(field_names case kind expect configure_rc compile_rc compile_state compile_marker verdict
                selected bindings edges slots provisions canonbind canonical_sha fingerprint categories_seen)

foreach(spec IN LISTS SOURCES)
    if(spec STREQUAL "")
        continue()
    endif()
    string(REPLACE "|" ";" spec_fields "${spec}")
    list(LENGTH spec_fields spec_len)
    if(NOT spec_len EQUAL 5)
        message(FATAL_ERROR "CompareFixtureMatrixRuns: SOURCES 每项必须是 label|cases|meta|exit|build_dir，当前：${spec}")
    endif()
    list(GET spec_fields 0 label)
    list(GET spec_fields 1 cases_path)
    list(GET spec_fields 2 meta_path)
    list(GET spec_fields 3 exit_path)
    list(GET spec_fields 4 build_dir)
    list(APPEND labels "${label}")

    # ---- run meta（配置身份 + 驱动器自述退出码）----
    if(NOT EXISTS "${meta_path}")
        string(APPEND problems "[${label}] 缺少 run meta：${meta_path}\n")
    else()
        file(READ "${meta_path}" meta_text)
        string(REPLACE "\r" "" meta_text "${meta_text}")
        string(REPLACE "\n" ";" meta_lines "${meta_text}")
        foreach(meta_line IN LISTS meta_lines)
            if(meta_line MATCHES "^([a-zA-Z_]+)=(.*)$")
                set("src_${src_index}_meta_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
            endif()
        endforeach()
    endif()

    # ---- 外部观察退出码（入口包装捕获值，不是驱动器自述）----
    set("src_${src_index}_observed_exit" "missing")
    if(EXISTS "${exit_path}")
        file(READ "${exit_path}" exit_text)
        string(REPLACE "\r" "" exit_text "${exit_text}")
        string(REPLACE "\n" ";" exit_lines "${exit_text}")
        foreach(exit_line IN LISTS exit_lines)
            if(exit_line MATCHES "^observed_exit=([0-9-]+)$")
                set("src_${src_index}_observed_exit" "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    else()
        string(APPEND problems "[${label}] 缺少驱动器外部退出码证据：${exit_path}\n")
    endif()
    set("src_${src_index}_build_dir" "${build_dir}")

    # ---- 逐 case 记录 ----
    if(NOT EXISTS "${cases_path}")
        string(APPEND problems "[${label}] 缺少 machine records：${cases_path}\n")
    else()
        file(READ "${cases_path}" cases_text)
        string(REPLACE "\r" "" cases_text "${cases_text}")
        string(REPLACE "\n" ";" case_lines "${cases_text}")
        set("src_${src_index}_case_order" "")
        set(local_case_count 0)
        foreach(case_line IN LISTS case_lines)
            if(case_line STREQUAL "" OR case_line MATCHES "^#")
                continue()
            endif()
            string(REPLACE "|" ";" case_fields "${case_line}")
            list(LENGTH case_fields case_field_count)
            if(NOT case_field_count EQUAL 17)
                string(APPEND problems "[${label}] machine records 字段数=${case_field_count}，期望 17：${case_line}\n")
                continue()
            endif()
            list(GET case_fields 0 this_case)
            math(EXPR local_case_count "${local_case_count} + 1")
            string(APPEND "src_${src_index}_case_order" "${this_case}\n")
            set(field_pos 0)
            foreach(field_name IN LISTS field_names)
                list(GET case_fields ${field_pos} field_value)
                set("c_${src_index}_${this_case}_${field_name}" "${field_value}")
                math(EXPR field_pos "${field_pos} + 1")
            endforeach()
        endforeach()
        set("src_${src_index}_case_count" "${local_case_count}")
    endif()

    math(EXPR src_index "${src_index} + 1")
endforeach()

list(LENGTH labels label_count)
if(label_count LESS 2)
    string(APPEND problems "对照至少需要 2 份配置记录，实际 ${label_count}\n")
endif()

# ---------------------------------------------------------------------------
# [1] 配置身份与外部退出码
# ---------------------------------------------------------------------------
set(identity_report "")
set(combo_list "")
math(EXPR last_index "${label_count} - 1")
foreach(idx RANGE 0 ${last_index})
    list(GET labels ${idx} label)
    set(meta_cxx_id  "${src_${idx}_meta_cxx_id}")
    set(meta_config  "${src_${idx}_meta_config}")
    set(meta_build_type "${src_${idx}_meta_build_type}")
    set(observed     "${src_${idx}_observed_exit}")
    set(case_count   "${src_${idx}_case_count}")
    set(passed       "${src_${idx}_meta_passed}")
    set(failed       "${src_${idx}_meta_failed}")
    set(verdict_exit "${src_${idx}_meta_driver_verdict_exit}")
    set(report_version "${src_${idx}_meta_report_version}")

    # 工具链粗分类：只用于"四配置矩阵完整性"，不参与逐 case 数值对照
    set(toolchain "unknown")
    if(meta_cxx_id MATCHES "GNU|Clang")
        set(toolchain "MinGW")
    elseif(meta_cxx_id STREQUAL "MSVC")
        set(toolchain "MSVC")
    endif()
    if(NOT "${meta_config}" STREQUAL "" AND NOT "${toolchain}" STREQUAL "unknown")
        list(APPEND combo_list "${toolchain}/${meta_config}")
    endif()

    # 多配置生成器没有 CMAKE_BUILD_TYPE：报告里写 n/a 而不是留空
    set(build_type_display "${meta_build_type}")
    if("${build_type_display}" STREQUAL "")
        set(build_type_display "n/a（多配置生成器按 config 区分）")
    endif()

    # 驱动器自述退出码必须与外部观察一致，否则"通过"结论自相矛盾
    if(NOT "${verdict_exit}" STREQUAL "${observed}")
        string(APPEND problems
            "[${label}] 驱动器自述退出码(${verdict_exit})与外部观察(${observed})不一致：报告结论不可信\n")
    endif()
    if(NOT "${observed}" STREQUAL "0")
        string(APPEND problems "[${label}] 外部退出码=${observed}（非 0：该配置的 fixture 矩阵未通过）\n")
    endif()

    string(APPEND identity_report
        "  ${label}\n"
        "    generator      : ${src_${idx}_meta_generator} ${src_${idx}_meta_generator_platform} ${src_${idx}_meta_generator_instance}\n"
        "    build type     : ${build_type_display}\n"
        "    config         : ${meta_config}\n"
        "    cxx            : ${meta_cxx_id} @ ${src_${idx}_meta_cxx}\n"
        "    report version : ${report_version}\n"
        "    cases          : ${case_count}（passed=${passed} failed=${failed}）\n"
        "    driver exit    : 外部观察=${observed}，驱动器自述=${verdict_exit}\n"
        "    build dir      : ${src_${idx}_build_dir}\n")
endforeach()

# ---------------------------------------------------------------------------
# [2] 四配置矩阵完整性
# ---------------------------------------------------------------------------
set(combo_report "")
foreach(wanted "MinGW/Debug" "MinGW/Release" "MSVC/Debug" "MSVC/Release")
    set(found FALSE)
    foreach(combo IN LISTS combo_list)
        if(combo STREQUAL wanted)
            set(found TRUE)
        endif()
    endforeach()
    if(found)
        string(APPEND combo_report "  ${wanted} : present\n")
    else()
        string(APPEND combo_report "  ${wanted} : MISSING\n")
        string(APPEND problems "四配置矩阵不完整：缺少 ${wanted}\n")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# [3] 逐 case 对照（必须一致的字段）+ [4] 逐格退出码（外部观察）
# ---------------------------------------------------------------------------
set(compare_report "")
set(exit_report "")
set(case_order "${src_0_case_order}")
string(REPLACE "\n" ";" case_name_list "${case_order}")
set(compared_cases 0)
set(compared_fields 0)

# 必须一致的字段（顺序即报告里的展示顺序）
set(strict_fields kind expect configure_rc compile_state compile_marker verdict
                   selected bindings edges slots provisions canonbind canonical_sha fingerprint categories_seen)

foreach(this_case IN LISTS case_name_list)
    if(this_case STREQUAL "")
        continue()
    endif()
    math(EXPR compared_cases "${compared_cases} + 1")
    set(line_values "")
    set(line_state "same")

    # 一致性判定 + 差异收集
    foreach(field_name IN LISTS strict_fields)
        set(first_value "${c_0_${this_case}_${field_name}}")
        set(field_values "")
        set(field_state "same")
        foreach(idx RANGE 0 ${last_index})
            list(GET labels ${idx} label)
            set(one_value "${c_${idx}_${this_case}_${field_name}}")
            if(one_value STREQUAL "")
                set(one_value "<missing>")
            endif()
            string(APPEND field_values "${label}=${one_value} ")
            if(NOT one_value STREQUAL first_value)
                set(field_state "DIFF")
            endif()
        endforeach()
        math(EXPR compared_fields "${compared_fields} + 1")
        if(field_state STREQUAL "DIFF")
            set(line_state "DIFF")
            string(APPEND problems
                "[${this_case}] 字段 ${field_name} 四配置不一致（必须解释）：${field_values}\n")
        endif()
    endforeach()

    # 对照行：指纹 / bindings-edges / 类别为必看项，其余按 label=值 并列
    set(fp_values "")
    set(be_values "")
    set(cat_values "")
    set(expect_values "")
    foreach(idx RANGE 0 ${last_index})
        string(APPEND fp_values "${c_${idx}_${this_case}_fingerprint},")
        string(APPEND be_values "${c_${idx}_${this_case}_bindings}/${c_${idx}_${this_case}_edges},")
        string(APPEND cat_values "${c_${idx}_${this_case}_categories_seen}+${c_${idx}_${this_case}_compile_marker},")
        string(APPEND expect_values "${c_${idx}_${this_case}_expect},")
    endforeach()
    string(REGEX REPLACE ",$" "" fp_values "${fp_values}")
    string(REGEX REPLACE ",$" "" be_values "${be_values}")
    string(REGEX REPLACE ",$" "" cat_values "${cat_values}")
    string(REGEX REPLACE ",$" "" expect_values "${expect_values}")

    string(APPEND compare_report
        "  ${this_case}（kind=${c_0_${this_case}_kind}）\n"
        "    expect        : ${expect_values}  [${line_state}]\n"
        "    fingerprint   : ${fp_values}\n"
        "    bindings/edges: ${be_values}\n"
        "    categories    : ${cat_values}（configure 期类别 + 编译期标记）\n")

    # 逐格退出码（原始值，允许不同；不作为一致性门槛）
    set(configure_values "")
    set(compile_values "")
    set(verdict_values "")
    foreach(idx RANGE 0 ${last_index})
        list(GET labels ${idx} label)
        string(APPEND configure_values "${label}=${c_${idx}_${this_case}_configure_rc} ")
        string(APPEND compile_values "${label}=${c_${idx}_${this_case}_compile_rc} ")
        string(APPEND verdict_values "${label}=${c_${idx}_${this_case}_verdict} ")
    endforeach()
    string(APPEND exit_report "  ${this_case}\n"
        "    configure 退出码 : ${configure_values}\n"
        "    compile 退出码   : ${compile_values}\n"
        "    verdict          : ${verdict_values}\n")
endforeach()

# 各配置的 case 集合必须完全一致（顺序也要一致，否则对照会错位）
foreach(idx RANGE 0 ${last_index})
    list(GET labels ${idx} label)
    set(other_order "${src_${idx}_case_order}")
    if(NOT other_order STREQUAL case_order)
        string(APPEND problems "[${label}] case 集合/顺序与 ${src_0_meta_config} 那一格不同（对照会错位）\n")
    endif()
endforeach()

string(TIMESTAMP run_started "%Y-%m-%d %H:%M:%S")

set(report_version_text "${src_0_meta_report_version}")

if(problems)
    set(exit_code 1)
    set(problem_text "${problems}")
else()
    set(exit_code 0)
    set(problem_text "无（全部必须一致的字段逐 case 相同；允许不同的字段见 [1] 与 [4]）\n")
endif()

set(sources_lines "")
foreach(spec IN LISTS SOURCES)
    if(NOT spec STREQUAL "")
        string(APPEND sources_lines "  - ${spec}\n")
    endif()
endforeach()

string(CONCAT report_text
    "=== Benchmark/validation/assembly — 四配置一致性对照（${report_version_text}）===\n"
    "generated        : ${run_started}\n"
    "tool             : ${CMAKE_CURRENT_LIST_FILE}\n"
    "scope            : 只比对多份配置各自的 machine records；本文件是校验资产报告，\n"
    "                   不写官方 <benchmark>/report.txt、不做性能测量、不产生评级、不替代行为 review\n"
    "sources          : ${label_count} 份\n"
    "${sources_lines}"
    "--------------------------------------------------------------------\n"
    "[1] 配置身份与外部退出码（逐格）\n"
    "${identity_report}"
    "--------------------------------------------------------------------\n"
    "[2] 四配置矩阵完整性（MinGW/MSVC × Debug/Release）\n"
    "${combo_report}"
    "--------------------------------------------------------------------\n"
    "[3] 逐 case 关键数值对照（列顺序 = 上面 sources 顺序 = ${labels}）\n"
    "${compare_report}"
    "--------------------------------------------------------------------\n"
    "[4] 逐 case 逐格退出码（configure / compile 原始外部退出码；允许不同，仅供逐格核对）\n"
    "${exit_report}"
    "--------------------------------------------------------------------\n"
    "[5] 不一致清单（必须解释）\n"
    "${problem_text}"
    "--------------------------------------------------------------------\n"
    "[6] 口径与局限\n"
    "  - 必须一致：kind / expect / configure 退出码 / 编译状态 / 编译期标记 / 判定结果 /\n"
    "    selected / bindings / edges / slots / provisions / canonbind / canonical-sha256 /\n"
    "    fingerprint / configure 期类别。这些字段由清单与解析/生成逻辑决定，**与编译器无关**，\n"
    "    所以任何差异都是真差异（清单语义、解析器行为或生成物不同），必须逐条解释。\n"
    "  - 允许不同：generator / build type / config / cxx / driver 外部退出码 / 编译子进程原始退出码\n"
    "    （编译器错误码数值不保证相同，语义由「编译状态」归一化后比较）。\n"
    "  - 本对照不判定性能、不产生评级；也不替代 IDAssemblyValidationRun 的真实 target/产物/运行期取证。\n"
    "--------------------------------------------------------------------\n"
    "SUMMARY\n"
    "  sources          : ${label_count}（${labels}）\n"
    "  compared cases   : ${compared_cases}\n"
    "  compared fields  : ${compared_fields}（必须一致的字段 × case × 配置数）\n"
    "  inconsistent     : ${problem_text}\n"
    "  exit code        : ${exit_code}\n")

get_filename_component(out_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${out_dir}")
file(WRITE "${OUT}" "${report_text}")

message(STATUS "fixture-matrix-compare: sources=${label_count} cases=${compared_cases} -> ${OUT}")
message(STATUS "fixture-matrix-compare: exit code=${exit_code}")

if(exit_code EQUAL 0)
    message(STATUS "四配置一致性对照通过：必须一致的字段逐 case 相同，四格齐全")
else()
    message(STATUS "四配置一致性对照发现问题：\n${problems}")
    message(FATAL_ERROR "四配置一致性对照未通过（详见 ${OUT}）")
endif()
