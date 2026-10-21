# P1 生产 API 反向编译用例驱动器（cmake -P）
#
# 判定口径（与 P0 一致，缺一不可）：
#   1. 先构建同环境合法对照 IDProductionLegalCases 与正向对照 IDProductionPositiveCases，
#      两者都必须退出码 0；再构建并运行 ProductionCaseControls，必须退出码 0；
#   2. 每个期望失败目标必须退出码非 0；
#   3. 失败输出必须出现静态断言失败文本（GNU 措辞 "static assertion failed"）；
#   4. 静态断言失败条数必须恰好 1：辅助断言单独失败会被拒绝；
#   5. 必须出现 ID_ASSEMBLY_ERROR_EXPECTED:<类别>（实际类别 == 期望类别才出现）；
#   6. 不得出现 ID_ASSEMBLY_ERROR_MISMATCH（实际类别 != 期望类别时出现）；
#   7. 驱动器自检三条：
#      a. wrong_expectation 必须因 MISMATCH 被拒绝；
#      b. aux_assert_failure 必须因"失败断言数 != 1"被拒绝；
#      c. 正向注册检查在"故意漏掉一个已存在的正向 TU"时必须拒绝（missing-positive 自检），
#         且 positive 源文件的 basename 必须真的出现在本次编译输出里。
#   8. 被篡改的正向断言目标 production_positive_mutated 必须编译失败且恰好 1 条断言。
#
# 日志布局：raw/production-<构建目录名>/ 下保存每条用例完整编译输出、命令、CWD、退出码与索引；
#   同一目录由 batch-owner.txt 绑定单一构建目录，禁止两个构建目录静默混写。
#
# 已知限制：断言文本与计数依赖编译器输出格式，本机只在 GCC 16.2.0 实测；
#   MSVC / clang 未安装、未验证，不能把本批 PASS 结论平移过去。

if(NOT PRODUCTION_BINARY_DIR)
    message(FATAL_ERROR "RunProductionCases: 缺少 PRODUCTION_BINARY_DIR")
endif()
if(NOT CASES)
    message(FATAL_ERROR "RunProductionCases: 缺少 CASES")
endif()
if(NOT RAW_LOG_DIR)
    message(FATAL_ERROR "RunProductionCases: 缺少 RAW_LOG_DIR")
endif()

set(ASSERT_FAIL_TEXT "static assertion failed")
set(EXPECTED_TAG    "ID_ASSEMBLY_ERROR_EXPECTED:")
set(MISMATCH_TAG    "ID_ASSEMBLY_ERROR_MISMATCH")

if(NOT RAW_LOG_MODE)
    set(RAW_LOG_MODE "production-unnamed-mode")
endif()
foreach(meta_var BUILD_DIR BATCH_CWD BUILD_TYPE MONOLITHIC GENERATOR CXX_COMPILER CXX_COMPILER_ID)
    if("${${meta_var}}" STREQUAL "")
        set(${meta_var} "unknown")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RAW_LOG_DIR}")
set(index_path "${RAW_LOG_DIR}/negative-index.txt")
set(owner_path "${RAW_LOG_DIR}/batch-owner.txt")

if(EXISTS "${owner_path}")
    file(STRINGS "${owner_path}" owner_lines)
    if(owner_lines)
        list(GET owner_lines 0 previous_build_dir)
        if(NOT "${previous_build_dir}" STREQUAL "${BUILD_DIR}")
            message(FATAL_ERROR
                "production 日志目录 ${RAW_LOG_DIR} 已属于另一个构建目录：\n"
                "  已记录：${previous_build_dir}\n  当前  ：${BUILD_DIR}\n"
                "同一目录不允许被两个构建目录共用。请换构建目录名，或人工确认后删除归属文件。")
        endif()
    endif()
endif()
file(WRITE "${owner_path}" "${BUILD_DIR}\n")

string(TIMESTAMP batch_start "%Y-%m-%d %H:%M:%S")
set(batch_meta "")
string(APPEND batch_meta "MODE          : ${RAW_LOG_MODE}\n")
string(APPEND batch_meta "BUILD_DIR     : ${BUILD_DIR}\n")
string(APPEND batch_meta "BATCH_CWD     : ${BATCH_CWD}\n")
string(APPEND batch_meta "BUILD_TYPE    : ${BUILD_TYPE}\n")
string(APPEND batch_meta "MONOLITHIC    : ${MONOLITHIC}\n")
string(APPEND batch_meta "GENERATOR     : ${GENERATOR}\n")
string(APPEND batch_meta "CXX           : ${CXX_COMPILER}\n")
string(APPEND batch_meta "CXX_ID        : ${CXX_COMPILER_ID}\n")
string(APPEND batch_meta "DRIVER        : ${CMAKE_CURRENT_LIST_FILE}\n")
string(APPEND batch_meta "STARTED       : ${batch_start}\n")

set(control_target "ProductionCaseControls")
if(CONTROL_TARGET)
    set(control_target "${CONTROL_TARGET}")
endif()
set(legal_object_target "IDProductionLegalCases")
if(LEGAL_OBJECT_TARGET)
    set(legal_object_target "${LEGAL_OBJECT_TARGET}")
endif()
set(positive_object_target "IDProductionPositiveCases")
if(POSITIVE_OBJECT_TARGET)
    set(positive_object_target "${POSITIVE_OBJECT_TARGET}")
endif()
set(mutated_target "production_positive_mutated")
if(MUTATED_ASSERT_TARGET)
    set(mutated_target "${MUTATED_ASSERT_TARGET}")
endif()

set(summary_lines "")
set(index_rows "")
set(fatal_problems "")

string(REPLACE "," ";" case_entries "${CASES}")
list(LENGTH case_entries case_count)

# ---- 0. 工具函数 ------------------------------------------------------------

# 逐条核对"源文件 basename 出现在本次编译输出里"，防止注册了目标但实际没有编译某 TU。
function(check_basenames log_text prefix names out_missing)
    set(missing "")
    foreach(name IN LISTS names)
        string(FIND "${log_text}" "${prefix}${name}.cpp.obj" hit)
        if(hit EQUAL -1)
            list(APPEND missing "${prefix}${name}.cpp")
        endif()
    endforeach()
    set(${out_missing} "${missing}" PARENT_SCOPE)
endfunction()

# 正向 TU 注册检查：目录里的每个 positive_*.cpp 都必须出现在注册清单里。
function(check_positive_registration registered_list source_dir out_ok out_reason)
    file(GLOB on_disk "${source_dir}/positive_*.cpp")
    set(unregistered "")
    foreach(path IN LISTS on_disk)
        get_filename_component(basename "${path}" NAME_WE)
        list(FIND registered_list "${basename}" position)
        if(position EQUAL -1)
            list(APPEND unregistered "${basename}.cpp")
        endif()
    endforeach()
    set(missing_files "")
    foreach(name IN LISTS registered_list)
        if(NOT EXISTS "${source_dir}/${name}.cpp")
            list(APPEND missing_files "${name}.cpp")
        endif()
    endforeach()
    if(unregistered)
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_reason} "存在未注册的正向 TU：${unregistered}" PARENT_SCOPE)
    elseif(missing_files)
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_reason} "注册清单里的正向 TU 不存在：${missing_files}" PARENT_SCOPE)
    else()
        set(${out_ok} TRUE PARENT_SCOPE)
        set(${out_reason} "" PARENT_SCOPE)
    endif()
endfunction()

# ---- 1. 正向 TU 注册检查 + 自检（missing-positive） --------------------------
if(POSITIVE_NAMES AND POSITIVE_SOURCE_DIR)
    check_positive_registration("${POSITIVE_NAMES}" "${POSITIVE_SOURCE_DIR}" positive_ok positive_reason)
    file(WRITE "${RAW_LOG_DIR}/positive-registration.log"
        "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nREGISTERED: ${POSITIVE_NAMES}\n"
        "SOURCE_DIR: ${POSITIVE_SOURCE_DIR}\nRESULT: ${positive_ok}\nREASON: ${positive_reason}\n")
    if(positive_ok)
        string(APPEND summary_lines "[positive-registration] 全部正向 TU 已注册（${POSITIVE_NAMES}）\n")
        string(APPEND index_rows "POSITIVE_REG|registered|-|-|-|-|-|-|OK|positive-registration.log\n")
    else()
        string(APPEND summary_lines "[positive-registration] FAILED: ${positive_reason}\n")
        string(APPEND index_rows "POSITIVE_REG|registered|-|-|-|-|-|-|FAILED(${positive_reason})|positive-registration.log\n")
        string(APPEND fatal_problems "正向注册检查失败：${positive_reason}\n")
    endif()

    # 自检：从清单里去掉最后一项，检查函数必须拒绝；否则说明检查本身失效
    set(truncated_names "${POSITIVE_NAMES}")
    list(LENGTH truncated_names truncated_count)
    math(EXPR truncated_last "${truncated_count} - 1")
    list(REMOVE_AT truncated_names ${truncated_last})
    check_positive_registration("${truncated_names}" "${POSITIVE_SOURCE_DIR}" selfcheck_ok selfcheck_reason)
    file(WRITE "${RAW_LOG_DIR}/selfcheck-missing-positive.log"
        "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nTRUNCATED: ${truncated_names}\n"
        "SOURCE_DIR: ${POSITIVE_SOURCE_DIR}\nRESULT: ${selfcheck_ok}\nREASON: ${selfcheck_reason}\n")
    if(selfcheck_ok)
        string(APPEND summary_lines
            "[self-check] missing-positive -> FAILED: 去掉一个正向 TU 后注册检查仍然通过（检查失效）\n")
        string(APPEND index_rows "SELFCHECK|missing-positive|-|-|-|-|-|-|FAILED(check-not-effective)|selfcheck-missing-positive.log\n")
        string(APPEND fatal_problems "missing-positive 自检失效：漏注册没有被检查出来\n")
    else()
        string(APPEND summary_lines "[self-check] missing-positive -> 已按预期拒绝（${selfcheck_reason}）\n")
        string(APPEND index_rows "SELFCHECK|missing-positive|-|-|-|-|-|-|OK|selfcheck-missing-positive.log\n")
    endif()
else()
    string(APPEND fatal_problems "缺少 POSITIVE_NAMES / POSITIVE_SOURCE_DIR，无法核对正向 TU 注册\n")
endif()

# ---- 2. 强制重编译合法/正向对照的目标文件（只限测试目标自身） -----------------
if(FORCE_REBUILD_CONTROLS)
    foreach(object_target IN ITEMS "${legal_object_target}" "${positive_object_target}")
        file(GLOB stale_objects "${PRODUCTION_BINARY_DIR}/CMakeFiles/${object_target}.dir/src/*/*.obj")
        if(stale_objects)
            file(REMOVE ${stale_objects})
        endif()
    endforeach()
endif()

# ---- 3. 合法对照（全部反向用例的合法分支）+ 正向 TU，均为真实编译 --------------
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${PRODUCTION_BINARY_DIR}" --target "${legal_object_target}"
    RESULT_VARIABLE legal_result
    OUTPUT_VARIABLE legal_out
    ERROR_VARIABLE legal_err)
file(WRITE "${RAW_LOG_DIR}/legal-objects.log"
    "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nCWD: ${BATCH_CWD}\nTARGET: ${legal_object_target}\n"
    "COMMAND: ${CMAKE_COMMAND} --build ${PRODUCTION_BINARY_DIR} --target ${legal_object_target}\n"
    "EXIT: ${legal_result}\nEXPECTED_TUS: ${case_count}\n\n${legal_out}\n${legal_err}\n")

if(NOT legal_result EQUAL 0)
    string(APPEND fatal_problems "合法对照构建失败（exit=${legal_result}）：见 legal-objects.log\n")
    string(APPEND index_rows "LEGAL|${legal_object_target}|-|-|${legal_result}|-|-|-|FAILED(build)|legal-objects.log\n")
else()
    # case_entries 的每一项是 "name|marker"，先切成纯名字
    set(legal_names "")
    foreach(entry IN LISTS case_entries)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 case_name)
        list(APPEND legal_names "${case_name}")
    endforeach()
    check_basenames("${legal_out}\n${legal_err}" "case_" "${legal_names}" legal_missing)
    if(legal_missing)
        string(APPEND fatal_problems "合法对照缺少本次编译证据的 TU：${legal_missing}\n")
        string(APPEND index_rows "LEGAL|${legal_object_target}|-|-|0|-|-|-|FAILED(missing-tu)|legal-objects.log\n")
    else()
        string(APPEND summary_lines "[legal-objects] ${legal_object_target} -> exit 0，${case_count} 个 TU 均有编译输出\n")
        string(APPEND index_rows "LEGAL|${legal_object_target}|-|-|0|-|-|-|OK|legal-objects.log\n")
    endif()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${PRODUCTION_BINARY_DIR}" --target "${positive_object_target}"
    RESULT_VARIABLE positive_result
    OUTPUT_VARIABLE positive_out
    ERROR_VARIABLE positive_err)
file(WRITE "${RAW_LOG_DIR}/positive-objects.log"
    "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nCWD: ${BATCH_CWD}\nTARGET: ${positive_object_target}\n"
    "COMMAND: ${CMAKE_COMMAND} --build ${PRODUCTION_BINARY_DIR} --target ${positive_object_target}\n"
    "EXIT: ${positive_result}\nREGISTERED: ${POSITIVE_NAMES}\n\n${positive_out}\n${positive_err}\n")

if(NOT positive_result EQUAL 0)
    string(APPEND fatal_problems "正向 TU 构建失败（exit=${positive_result}）：见 positive-objects.log\n")
    string(APPEND index_rows "POSITIVE|${positive_object_target}|-|-|${positive_result}|-|-|-|FAILED(build)|positive-objects.log\n")
else()
    check_basenames("${positive_out}\n${positive_err}" "" "${POSITIVE_NAMES}" positive_missing)
    if(positive_missing)
        string(APPEND fatal_problems "正向 TU 缺少本次编译证据：${positive_missing}\n")
        string(APPEND index_rows "POSITIVE|${positive_object_target}|-|-|0|-|-|-|FAILED(missing-tu)|positive-objects.log\n")
    else()
        string(APPEND summary_lines "[positive-objects] ${positive_object_target} -> exit 0，6 个正向 TU 均有编译输出\n")
        string(APPEND index_rows "POSITIVE|${positive_object_target}|-|-|0|-|-|-|OK|positive-objects.log\n")
    endif()
endif()

# ---- 4. 构建合法对照可执行文件并检查链接响应文件确实包含全部合法+正向对象 ------
if(fatal_problems)
    # 前提未通过时不再把后面的失败用例算通过
    set(control_result 1)
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${PRODUCTION_BINARY_DIR}" --target "${control_target}" --verbose
        RESULT_VARIABLE control_result
        OUTPUT_VARIABLE control_out
        ERROR_VARIABLE control_err)
endif()

file(WRITE "${RAW_LOG_DIR}/negative-legal-control.log"
    "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nCWD: ${BATCH_CWD}\nTARGET: ${control_target}\n"
    "COMMAND: ${CMAKE_COMMAND} --build ${PRODUCTION_BINARY_DIR} --target ${control_target} --verbose\n"
    "EXIT: ${control_result}\n\n${control_out}\n${control_err}\n")

if(NOT control_result EQUAL 0)
    string(APPEND fatal_problems "合法对照可执行文件构建失败（exit=${control_result}）\n")
    string(APPEND index_rows "CONTROL|${control_target}|-|-|${control_result}|-|-|-|FAILED(build)|negative-legal-control.log\n")
else()
    # 链接输入以 CMake 生成的链接响应文件为准（MinGW 把 OBJECT 库对象汇总进 objects1.rsp）：
    # 计数直接从真正喂给链接器的清单读取，不从编译输出里猜。
    set(control_rsp "${PRODUCTION_BINARY_DIR}/CMakeFiles/${control_target}.dir/objects1.rsp")
    set(legal_link_count "-")
    set(positive_link_count "-")
    if(EXISTS "${control_rsp}")
        file(READ "${control_rsp}" control_link_text)
        string(REGEX MATCHALL "IDProductionLegalCases.dir" legal_link_hits "${control_link_text}")
        list(LENGTH legal_link_hits legal_link_count)
        string(REGEX MATCHALL "IDProductionPositiveCases.dir" positive_link_hits "${control_link_text}")
        list(LENGTH positive_link_hits positive_link_count)
        file(WRITE "${RAW_LOG_DIR}/control-link-inputs.log"
            "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nRSP: ${control_rsp}\n"
            "LEGAL_OBJECTS: ${legal_link_count}\nPOSITIVE_OBJECTS: ${positive_link_count}\n\n${control_link_text}\n")
    endif()
    if(NOT EXISTS "${control_rsp}")
        string(APPEND fatal_problems "找不到链接响应文件 ${control_rsp}（生成器未验证，不能声称对象已进入链接）\n")
        string(APPEND index_rows
            "CONTROL|${control_target}|-|-|0|-|-|-|FAILED(no-link-rsp)|negative-legal-control.log\n")
    elseif(NOT legal_link_count EQUAL case_count)
        string(APPEND fatal_problems
            "链接行里的合法对照对象数 ${legal_link_count} != ${case_count}（未真正链接 64 条合法分支）\n")
        string(APPEND index_rows
            "CONTROL|${control_target}|-|-|0|-|-|-|FAILED(link-count=${legal_link_count})|negative-legal-control.log\n")
    elseif(NOT positive_link_count EQUAL 6)
        string(APPEND fatal_problems
            "链接行里的正向对象数 ${positive_link_count} != 6（正向 TU 未真正链接）\n")
        string(APPEND index_rows
            "CONTROL|${control_target}|-|-|0|-|-|-|FAILED(positive-link-count=${positive_link_count})|negative-legal-control.log\n")
    else()
        string(APPEND summary_lines
            "[legal-control] ${control_target} -> exit 0，链接响应文件含 ${legal_link_count} 个合法对照对象 + ${positive_link_count} 个正向对象\n")

        # 运行合法对照程序：6 个正向探针 + 结构修订必须全部通过
        if(CONTROL_EXE AND EXISTS "${CONTROL_EXE}")
            execute_process(
                COMMAND "${CONTROL_EXE}"
                WORKING_DIRECTORY "${BATCH_CWD}"
                RESULT_VARIABLE control_run_result
                OUTPUT_VARIABLE control_run_out
                ERROR_VARIABLE control_run_err)
            file(WRITE "${RAW_LOG_DIR}/control-run.log"
                "MODE: ${RAW_LOG_MODE}\nCWD: ${BATCH_CWD}\nCOMMAND: ${CONTROL_EXE}\n"
                "EXIT: ${control_run_result}\n\n${control_run_out}\n${control_run_err}\n")
            if(NOT control_run_result EQUAL 0)
                string(APPEND fatal_problems "合法对照运行失败（exit=${control_run_result}）：见 control-run.log\n")
                string(APPEND index_rows "CONTROL_RUN|${control_target}|-|-|${control_run_result}|-|-|-|FAILED(run)|control-run.log\n")
            else()
                string(APPEND summary_lines "[control-run] ${control_target} -> exit 0（正向探针读数全部匹配）\n")
                string(APPEND index_rows "CONTROL_RUN|${control_target}|-|-|0|-|-|-|OK|control-run.log\n")
            endif()
        else()
            string(APPEND fatal_problems "CONTROL_EXE 不存在：${CONTROL_EXE}\n")
            string(APPEND index_rows "CONTROL_RUN|${control_target}|-|-|-|-|-|-|FAILED(no-exe)|-\n")
        endif()
    endif()
endif()

# ---- 5. 被篡改的正向断言必须被拒绝 ------------------------------------------
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${PRODUCTION_BINARY_DIR}" --target "${mutated_target}"
    RESULT_VARIABLE mutated_result
    OUTPUT_VARIABLE mutated_out
    ERROR_VARIABLE mutated_err)
set(mutated_log "${mutated_out}\n${mutated_err}")
string(REGEX MATCHALL "${ASSERT_FAIL_TEXT}" mutated_hits "${mutated_log}")
list(LENGTH mutated_hits mutated_assert_count)
string(FIND "${mutated_log}" "ID_POSITIVE_MUTATED_ASSERT_DETECTED" mutated_marker_hit)
set(mutated_marker_found FALSE)
if(mutated_marker_hit GREATER -1)
    set(mutated_marker_found TRUE)
endif()
file(WRITE "${RAW_LOG_DIR}/selfcheck-mutated-positive-assert.log"
    "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nTARGET: ${mutated_target}\n"
    "COMMAND: ${CMAKE_COMMAND} --build ${PRODUCTION_BINARY_DIR} --target ${mutated_target}\n"
    "EXIT: ${mutated_result}\nSTATIC_ASSERT_FAILED_COUNT: ${mutated_assert_count}\n"
    "MUTATED_MARKER_FOUND: ${mutated_marker_found}\n\n${mutated_log}\n")
if(mutated_result EQUAL 0 OR NOT mutated_assert_count EQUAL 1 OR mutated_marker_hit EQUAL -1)
    string(APPEND summary_lines
        "[self-check] mutated-positive-assert -> FAILED: 被篡改的正向断言没有被拒绝（exit=${mutated_result}, 断言数=${mutated_assert_count}）\n")
    string(APPEND index_rows
        "SELFCHECK|mutated-positive-assert|-|-|${mutated_result}|${mutated_assert_count}|-|-|FAILED|selfcheck-mutated-positive-assert.log\n")
    string(APPEND fatal_problems "mutated-positive-assert 自检失败：正向断言没有被真实求值\n")
else()
    string(APPEND summary_lines "[self-check] mutated-positive-assert -> 已按预期拒绝（失败断言数=1）\n")
    string(APPEND index_rows
        "SELFCHECK|mutated-positive-assert|-|-|${mutated_result}|1|-|-|OK|selfcheck-mutated-positive-assert.log\n")
endif()

# ---- 6. 单个期望失败目标判定 -------------------------------------------------
function(judge_expect_fail case_name marker out_ok out_reason out_log out_exit out_fail_count
         out_has_expected out_has_mismatch)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${PRODUCTION_BINARY_DIR}" --target "production_expect_${case_name}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_out
        ERROR_VARIABLE build_err)

    set(build_log "${build_out}\n${build_err}")
    string(REGEX MATCHALL "${ASSERT_FAIL_TEXT}" assert_hits "${build_log}")
    list(LENGTH assert_hits assert_fail_count)

    set(has_expected FALSE)
    if("${build_log}" MATCHES "${EXPECTED_TAG}${marker}")
        set(has_expected TRUE)
    endif()
    set(has_mismatch FALSE)
    if("${build_log}" MATCHES "${MISMATCH_TAG}")
        set(has_mismatch TRUE)
    endif()

    set(ok TRUE)
    set(reason "")
    if(build_result EQUAL 0)
        set(ok FALSE)
        set(reason "期望编译失败，实际构建成功")
    elseif(NOT "${build_log}" MATCHES "${ASSERT_FAIL_TEXT}")
        set(ok FALSE)
        set(reason "失败但不是静态断言（疑似环境/语法错误）")
    elseif(NOT assert_fail_count EQUAL 1)
        set(ok FALSE)
        set(reason "失败断言数=${assert_fail_count}（只允许类别断言失败）")
    elseif(has_mismatch)
        set(ok FALSE)
        set(reason "错误类别不符（出现 MISMATCH 标记）")
    elseif(NOT has_expected)
        set(ok FALSE)
        set(reason "缺少 EXPECTED:${marker} 标记（类别未被真实断言）")
    endif()

    file(WRITE "${RAW_LOG_DIR}/negative-${case_name}.log"
        "MODE: ${RAW_LOG_MODE}\nBUILD_DIR: ${BUILD_DIR}\nCWD: ${BATCH_CWD}\n"
        "TARGET: production_expect_${case_name}\nEXPECTED_MARKER: ${marker}\n"
        "COMMAND: ${CMAKE_COMMAND} --build ${PRODUCTION_BINARY_DIR} --target production_expect_${case_name}\n"
        "EXIT: ${build_result}\nSTATIC_ASSERT_FAILED_COUNT: ${assert_fail_count}\n"
        "HAS_EXPECTED: ${has_expected}\nHAS_MISMATCH: ${has_mismatch}\n\n${build_log}\n")

    set(${out_ok} ${ok} PARENT_SCOPE)
    set(${out_reason} "${reason}" PARENT_SCOPE)
    set(${out_log} "${build_log}" PARENT_SCOPE)
    set(${out_exit} ${build_result} PARENT_SCOPE)
    set(${out_fail_count} ${assert_fail_count} PARENT_SCOPE)
    set(${out_has_expected} ${has_expected} PARENT_SCOPE)
    set(${out_has_mismatch} ${has_mismatch} PARENT_SCOPE)
endfunction()

# ---- 7. 逐条期望失败用例 -----------------------------------------------------
set(failure_count 0)
set(passed_count 0)
if(NOT fatal_problems)
    foreach(entry IN LISTS case_entries)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 case_name)
        list(GET parts 1 marker)

        judge_expect_fail("${case_name}" "${marker}" case_ok case_reason case_log case_exit case_fail_count
            case_has_expected case_has_mismatch)
        if(case_ok)
            string(APPEND summary_lines
                "[expect-fail] ${case_name} -> category ${marker} OK（失败断言数=${case_fail_count}）\n")
            string(APPEND index_rows
                "CASE|${case_name}|${marker}|production_expect_${case_name}|${case_exit}|${case_fail_count}|${case_has_expected}|${case_has_mismatch}|OK|negative-${case_name}.log\n")
            math(EXPR passed_count "${passed_count} + 1")
        else()
            string(APPEND summary_lines "[expect-fail] ${case_name} -> FAILED: ${case_reason}\n")
            string(APPEND index_rows
                "CASE|${case_name}|${marker}|production_expect_${case_name}|${case_exit}|${case_fail_count}|${case_has_expected}|${case_has_mismatch}|FAILED(${case_reason})|negative-${case_name}.log\n")
            message(STATUS "---- 编译输出（${case_name}）----\n${case_log}")
            math(EXPR failure_count "${failure_count} + 1")
        endif()
    endforeach()
else()
    string(APPEND summary_lines "前提检查未通过：跳过全部期望失败用例（不把任何失败算通过）\n")
endif()

# ---- 8. 驱动器自检（错误类别 / 辅助断言） ------------------------------------
if(WRONG_EXPECT_CASE)
    judge_expect_fail("${WRONG_EXPECT_CASE}" "DuplicatePlugin" wrong_ok wrong_reason wrong_log wrong_exit
        wrong_fail_count wrong_has_expected wrong_has_mismatch)
    if(wrong_ok OR NOT wrong_reason MATCHES "MISMATCH")
        string(APPEND summary_lines
            "[self-check] ${WRONG_EXPECT_CASE} -> FAILED: 驱动器没有按 MISMATCH 拒绝错误类别（理由：${wrong_reason}）\n")
        math(EXPR failure_count "${failure_count} + 1")
    else()
        string(APPEND summary_lines "[self-check] ${WRONG_EXPECT_CASE} -> 已按预期拒绝（${wrong_reason}）\n")
    endif()
endif()

if(AUX_FAIL_CASE)
    judge_expect_fail("${AUX_FAIL_CASE}" "SelfDependency" aux_ok aux_reason aux_log aux_exit aux_fail_count
        aux_has_expected aux_has_mismatch)
    if(aux_ok OR NOT aux_reason MATCHES "失败断言数")
        string(APPEND summary_lines
            "[self-check] ${AUX_FAIL_CASE} -> FAILED: 辅助断言单独失败没有被抓住（理由：${aux_reason}）\n")
        math(EXPR failure_count "${failure_count} + 1")
    else()
        string(APPEND summary_lines "[self-check] ${AUX_FAIL_CASE} -> 已按预期拒绝（${aux_reason}）\n")
    endif()
endif()

string(APPEND summary_lines
    "反向用例汇总：${passed_count} 条按预期类别失败，${failure_count} 条不符合预期\n")
file(WRITE "${RAW_LOG_DIR}/negative-summary.log" "${summary_lines}")

string(TIMESTAMP batch_finish "%Y-%m-%d %H:%M:%S")
file(WRITE "${index_path}"
    "# production 反向用例批次索引（RunProductionCases.cmake 自动写入）\n"
    "# 判定：exit != 0 && 静态断言失败数 == 1 && 含 EXPECTED:<类别> && 不含 MISMATCH\n"
    "${batch_meta}"
    "FINISHED      : ${batch_finish}\n"
    "PASSED        : ${passed_count}\n"
    "FAILED        : ${failure_count}\n"
    "EXPECTED_TOTAL: ${case_count}\n"
    "LEGAL_CONTROL : ${legal_object_target} + ${positive_object_target} + ${control_target} run\n"
    "SELF_CHECKS   : ${WRONG_EXPECT_CASE}=MISMATCH-rejected, ${AUX_FAIL_CASE}=assert-count-rejected, missing-positive=registration-rejected, mutated-positive-assert=assert-rejected\n"
    "COLUMNS       : KIND|CASE|MARKER|TARGET|EXIT|ASSERT_FAIL_COUNT|HAS_EXPECTED|HAS_MISMATCH|VERDICT|LOG\n"
    "${index_rows}")

if(fatal_problems)
    message(FATAL_ERROR "production 批次前提/自检失败：\n${fatal_problems}")
endif()
if(failure_count GREATER 0)
    message(FATAL_ERROR "production 反向用例批次失败：${failure_count} 条未按预期类别失败（详见 ${RAW_LOG_DIR}）")
endif()
message(STATUS "production 反向用例批次通过（合法+正向对照 + 恰好 1 条断言 + 类别真实断言 + 4 个自检）")
