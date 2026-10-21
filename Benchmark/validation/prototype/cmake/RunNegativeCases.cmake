# 反向编译用例驱动器（cmake -P）
#
# 判定口径（缺一不可，避免把环境失败、"任意错误"或"辅助断言失败"算成通过）：
#   1. 先构建同环境的"合法对照"目标，必须退出码 0；否则直接判整个批次环境损坏；
#   2. 每个期望失败目标必须退出码非 0；
#   3. 失败输出里必须出现静态断言失败文本（确认是静态断言而不是链接/缺头文件）。
#      该文本取 GNU 工具链的措辞 "static assertion failed"：本机只有 GCC 16.2.0 实测过；
#      MSVC / clang 未安装、未验证，措辞或计数方式可能不同，因此本判据与"批通过"结论
#      **只在 GCC 16.2.0 上成立**，不宣称其它编译器通过。
#   4. 静态断言失败条数必须**恰好 1 条**：每条用例只有"类别断言"允许失败。
#      辅助断言（环闭合、成员集合、边界前置、身份合法性等）若单独失败，会额外产生一条
#      失败文本；此时即使 EXPECTED 出现、MISMATCH 不出现也判失败。
#      这既堵住"类别正确但辅助断言炸了"的假通过，也堵住"删掉辅助断言换通过"的做法
#      （自检 aux_assert_failure 专门验证这条判据真的有牙齿）。
#   5. 失败输出里必须出现 ID_ASSEMBLY_ERROR_EXPECTED:<类别> —— 它只在
#      "实际错误类别 == 期望类别" 时才会出现（见 CaseCommon.hpp 的互斥断言对）；
#   6. 失败输出里不得出现 ID_ASSEMBLY_ERROR_MISMATCH —— 它在实际类别 != 期望类别时出现；
#   7. 驱动器自检（两条，均不计入 28 条反向用例）：
#        - wrong_expectation：期望类别被故意写错，必须因 MISMATCH 被拒绝；
#        - aux_assert_failure：类别断言正确、但一条辅助断言也失败，必须因
#          "失败断言数 != 1" 被拒绝。
#
# 日志布局：每条用例的完整编译输出、命令、工作目录与退出码写入 RAW_LOG_DIR。
#   RAW_LOG_DIR 由 prototype/CMakeLists.txt 按构建模式派生（raw/protoshared、raw/protostatic 等），
#   批次索引 negative-index.txt 与汇总 negative-summary.log 同样写在该目录，
#   SHARED / STATIC 各写各的目录，日志与 summary 都不需要人工改名，也不会互相覆盖。
#   索引里记录 BUILD_DIR / BATCH_CWD / 工具链与逐条判定列；若同一目录被另一个构建目录复用，
#   本脚本直接报错停止，而不是静默混写。
#
# 用法：
#   cmake -DPROTOTYPE_BINARY_DIR=<build dir> -DRAW_LOG_DIR=<模式日志目录> \
#         -DRAW_LOG_MODE=<模式名> -DBATCH_CWD=<批次工作目录> -DBUILD_DIR=<构建目录> \
#         -DBUILD_TYPE=<配置> -DMONOLITHIC=<ON|OFF> -DGENERATOR=<生成器> \
#         -DCXX_COMPILER=<编译器路径> -DCXX_COMPILER_ID=<编译器身份> \
#         -DCASES="case|Marker,..." -DWRONG_EXPECT_CASE=wrong_expectation \
#         -DAUX_FAIL_CASE=aux_assert_failure -P RunNegativeCases.cmake
#
# 已知限制：静态断言失败文本与"失败断言数"依赖编译器输出格式，本脚本只在本机 GCC 16.2.0 上实测；
#          其它编译器必须重新核实文本与计数方式，不能把本批次的 PASS 结论平移过去。

if(NOT PROTOTYPE_BINARY_DIR)
    message(FATAL_ERROR "RunNegativeCases: 缺少 PROTOTYPE_BINARY_DIR")
endif()
if(NOT CASES)
    message(FATAL_ERROR "RunNegativeCases: 缺少 CASES")
endif()
if(NOT RAW_LOG_DIR)
    message(FATAL_ERROR "RunNegativeCases: 缺少 RAW_LOG_DIR（完整编译输出必须留证）")
endif()

set(ASSERT_FAIL_TEXT "static assertion failed")
set(EXPECTED_TAG    "ID_ASSEMBLY_ERROR_EXPECTED:")
set(MISMATCH_TAG    "ID_ASSEMBLY_ERROR_MISMATCH")

if(NOT RAW_LOG_MODE)
    set(RAW_LOG_MODE "unnamed-mode")
endif()
foreach(meta_var BUILD_DIR BATCH_CWD BUILD_TYPE MONOLITHIC GENERATOR CXX_COMPILER CXX_COMPILER_ID)
    if("${${meta_var}}" STREQUAL "")
        set(${meta_var} "unknown")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RAW_LOG_DIR}")

set(index_path "${RAW_LOG_DIR}/negative-index.txt")
set(owner_path "${RAW_LOG_DIR}/batch-owner.txt")

# 同一模式目录只能属于一个构建目录：发现别的构建目录写过，直接停，避免两套日志静默混在一起
if(EXISTS "${owner_path}")
    file(STRINGS "${owner_path}" owner_lines)
    if(owner_lines)
        list(GET owner_lines 0 previous_build_dir)
        if(NOT "${previous_build_dir}" STREQUAL "${BUILD_DIR}")
            message(FATAL_ERROR
                "反向用例日志目录 ${RAW_LOG_DIR} 已属于另一个构建目录：\n"
                "  已记录：${previous_build_dir}\n"
                "  当前  ：${BUILD_DIR}\n"
                "同一模式目录不允许被两个构建目录共用（会互相覆盖）。"
                "请使用目录名不同的构建目录，或先人工确认后删除该归属文件。")
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

set(control_target "IDFixtureLegalCases")
if(CONTROL_TARGET)
    set(control_target "${CONTROL_TARGET}")
endif()

set(summary_lines "")
set(index_rows "")

# ---- 1. 同环境合法对照 ----
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${PROTOTYPE_BINARY_DIR}" --target "${control_target}"
    RESULT_VARIABLE control_result
    OUTPUT_VARIABLE control_out
    ERROR_VARIABLE control_err)

file(WRITE "${RAW_LOG_DIR}/negative-legal-control.log"
    "MODE: ${RAW_LOG_MODE}\n"
    "BUILD_DIR: ${BUILD_DIR}\n"
    "CWD: ${BATCH_CWD}\n"
    "TARGET: ${control_target}\n"
    "COMMAND: ${CMAKE_COMMAND} --build ${PROTOTYPE_BINARY_DIR} --target ${control_target}\n"
    "EXIT: ${control_result}\n\n${control_out}\n${control_err}\n")

if(NOT control_result EQUAL 0)
    message(FATAL_ERROR
        "合法对照构建失败（环境问题，拒绝把后面的失败用例算通过）：\n${control_out}\n${control_err}")
endif()
string(APPEND summary_lines "[legal-control] ${control_target} -> exit 0\n")
string(APPEND index_rows
    "CONTROL|${control_target}|-|${control_target}|0|0|n/a|n/a|OK|negative-legal-control.log\n")
message(STATUS "[legal-control] ${control_target} -> exit 0")

# ---- 2. 单个期望失败目标的判定（返回 ok / reason / 日志全文 / 退出码 / 失败断言数） ----
function(judge_expect_fail case_name marker out_ok out_reason out_log out_exit out_fail_count
         out_has_expected out_has_mismatch)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${PROTOTYPE_BINARY_DIR}" --target "fixture_expect_${case_name}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_out
        ERROR_VARIABLE build_err)

    set(build_log "${build_out}\n${build_err}")

    # 静态断言失败条数：GNU 文本，每失败一条断言出现一次；本机 GCC 16.2.0 实测
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
        set(reason "失败断言数=${assert_fail_count}（判据要求恰好 1 条：只允许类别断言失败，辅助断言不得单独失败）")
    elseif(has_mismatch)
        set(ok FALSE)
        set(reason "错误类别不符（出现 MISMATCH 标记）")
    elseif(NOT has_expected)
        set(ok FALSE)
        set(reason "缺少 EXPECTED:${marker} 标记（类别未被真实断言）")
    endif()

    file(WRITE "${RAW_LOG_DIR}/negative-${case_name}.log"
        "MODE: ${RAW_LOG_MODE}\n"
        "BUILD_DIR: ${BUILD_DIR}\n"
        "CWD: ${BATCH_CWD}\n"
        "TARGET: fixture_expect_${case_name}\n"
        "EXPECTED_MARKER: ${marker}\n"
        "COMMAND: ${CMAKE_COMMAND} --build ${PROTOTYPE_BINARY_DIR} --target fixture_expect_${case_name}\n"
        "EXIT: ${build_result}\n"
        "STATIC_ASSERT_FAILED_COUNT: ${assert_fail_count}\n"
        "HAS_EXPECTED: ${has_expected}\n"
        "HAS_MISMATCH: ${has_mismatch}\n\n${build_log}\n")

    set(${out_ok} ${ok} PARENT_SCOPE)
    set(${out_reason} "${reason}" PARENT_SCOPE)
    set(${out_log} "${build_log}" PARENT_SCOPE)
    set(${out_exit} ${build_result} PARENT_SCOPE)
    set(${out_fail_count} ${assert_fail_count} PARENT_SCOPE)
    set(${out_has_expected} ${has_expected} PARENT_SCOPE)
    set(${out_has_mismatch} ${has_mismatch} PARENT_SCOPE)
endfunction()

# ---- 3. 逐个期望失败用例 ----
string(REPLACE "," ";" case_entries "${CASES}")

set(failure_count 0)
set(passed_count 0)
foreach(entry IN LISTS case_entries)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 case_name)
    list(GET parts 1 marker)

    judge_expect_fail("${case_name}" "${marker}" case_ok case_reason case_log case_exit case_fail_count
        case_has_expected case_has_mismatch)
    if(case_ok)
        string(APPEND summary_lines
            "[expect-fail] ${case_name} -> category ${marker} OK（失败断言数=${case_fail_count}, log=negative-${case_name}.log）\n")
        string(APPEND index_rows
            "CASE|${case_name}|${marker}|fixture_expect_${case_name}|${case_exit}|${case_fail_count}|${case_has_expected}|${case_has_mismatch}|OK|negative-${case_name}.log\n")
        message(STATUS "[expect-fail] ${case_name} -> category ${marker} OK")
        math(EXPR passed_count "${passed_count} + 1")
    else()
        string(APPEND summary_lines "[expect-fail] ${case_name} -> FAILED: ${case_reason}\n")
        string(APPEND index_rows
            "CASE|${case_name}|${marker}|fixture_expect_${case_name}|${case_exit}|${case_fail_count}|${case_has_expected}|${case_has_mismatch}|FAILED(${case_reason})|negative-${case_name}.log\n")
        message(STATUS "[expect-fail] ${case_name} -> FAILED: ${case_reason}")
        message(STATUS "---- 编译输出（${case_name}）----\n${case_log}")
        math(EXPR failure_count "${failure_count} + 1")
    endif()
endforeach()

# ---- 4. 驱动器自检（都不计入 28 条反向用例） ----
# 4.1 期望类别写错的目标必须因 MISMATCH 被拒绝；拒绝理由必须是"类别不符"，不能是环境错误
if(WRONG_EXPECT_CASE)
    judge_expect_fail("${WRONG_EXPECT_CASE}" "DuplicatePlugin" wrong_ok wrong_reason wrong_log wrong_exit
        wrong_fail_count wrong_has_expected wrong_has_mismatch)
    if(wrong_ok OR NOT wrong_reason MATCHES "MISMATCH")
        string(APPEND summary_lines
            "[self-check] ${WRONG_EXPECT_CASE} -> FAILED: 驱动器没有按 MISMATCH 拒绝错误类别（判定逻辑失效，理由：${wrong_reason}）\n")
        string(APPEND index_rows
            "SELF|${WRONG_EXPECT_CASE}|DuplicatePlugin|fixture_expect_${WRONG_EXPECT_CASE}|${wrong_exit}|${wrong_fail_count}|${wrong_has_expected}|${wrong_has_mismatch}|FAILED(未按 MISMATCH 拒绝)|negative-${WRONG_EXPECT_CASE}.log\n")
        message(STATUS "[self-check] ${WRONG_EXPECT_CASE} -> FAILED: 驱动器没有按 MISMATCH 拒绝错误类别（理由：${wrong_reason}）")
        math(EXPR failure_count "${failure_count} + 1")
    else()
        string(APPEND summary_lines
            "[self-check] ${WRONG_EXPECT_CASE} -> 已按预期拒绝（${wrong_reason}；失败断言数=${wrong_fail_count}）\n")
        string(APPEND index_rows
            "SELF|${WRONG_EXPECT_CASE}|DuplicatePlugin|fixture_expect_${WRONG_EXPECT_CASE}|${wrong_exit}|${wrong_fail_count}|${wrong_has_expected}|${wrong_has_mismatch}|REJECTED_OK(${wrong_reason})|negative-${WRONG_EXPECT_CASE}.log\n")
        message(STATUS "[self-check] ${WRONG_EXPECT_CASE} -> 已按预期拒绝（${wrong_reason}）")
    endif()
endif()

# 4.2 类别正确但辅助断言也失败的目标必须因"失败断言数 != 1"被拒绝。
#     这一条同时证明：反向用例的通过不是靠删掉辅助断言换来的——判据会抓住辅助断言失败。
if(AUX_FAIL_CASE)
    judge_expect_fail("${AUX_FAIL_CASE}" "SelfDependency" aux_ok aux_reason aux_log aux_exit aux_fail_count
        aux_has_expected aux_has_mismatch)
    if(aux_ok OR NOT aux_reason MATCHES "失败断言数")
        string(APPEND summary_lines
            "[self-check] ${AUX_FAIL_CASE} -> FAILED: 辅助断言单独失败没有被判据抓住（理由：${aux_reason}）\n")
        string(APPEND index_rows
            "SELF|${AUX_FAIL_CASE}|SelfDependency|fixture_expect_${AUX_FAIL_CASE}|${aux_exit}|${aux_fail_count}|${aux_has_expected}|${aux_has_mismatch}|FAILED(未按失败断言数拒绝)|negative-${AUX_FAIL_CASE}.log\n")
        message(STATUS "[self-check] ${AUX_FAIL_CASE} -> FAILED: 辅助断言单独失败没有被判据抓住（理由：${aux_reason}）")
        math(EXPR failure_count "${failure_count} + 1")
    else()
        string(APPEND summary_lines
            "[self-check] ${AUX_FAIL_CASE} -> 已按预期拒绝（${aux_reason}）\n")
        string(APPEND index_rows
            "SELF|${AUX_FAIL_CASE}|SelfDependency|fixture_expect_${AUX_FAIL_CASE}|${aux_exit}|${aux_fail_count}|${aux_has_expected}|${aux_has_mismatch}|REJECTED_OK(${aux_reason})|negative-${AUX_FAIL_CASE}.log\n")
        message(STATUS "[self-check] ${AUX_FAIL_CASE} -> 已按预期拒绝（${aux_reason}）")
    endif()
endif()

string(APPEND summary_lines
    "反向用例汇总：${passed_count} 条按预期类别失败，${failure_count} 条不符合预期\n")
file(WRITE "${RAW_LOG_DIR}/negative-summary.log" "${summary_lines}")

string(TIMESTAMP batch_finish "%Y-%m-%d %H:%M:%S")
file(WRITE "${index_path}"
    "# 反向用例批次可复核索引（RunNegativeCases.cmake 自动写入；SHARED / STATIC 各一份，互不覆盖）\n"
    "# 判定：exit != 0 && 静态断言失败条数 == 1 && 含 EXPECTED:<类别> && 不含 MISMATCH\n"
    "${batch_meta}"
    "FINISHED      : ${batch_finish}\n"
    "PASSED        : ${passed_count}\n"
    "FAILED        : ${failure_count}\n"
    "LEGAL_CONTROL : ${control_target} exit 0\n"
    "SELF_CHECKS   : ${WRONG_EXPECT_CASE}=MISMATCH-rejected, ${AUX_FAIL_CASE}=assert-count-rejected（均不计入 28 条）\n"
    "COLUMNS       : KIND|CASE|MARKER|TARGET|EXIT|ASSERT_FAIL_COUNT|HAS_EXPECTED(TRUE/FALSE)|HAS_MISMATCH(TRUE/FALSE)|VERDICT|LOG\n"
    "${index_rows}")

if(failure_count GREATER 0)
    message(FATAL_ERROR "反向用例批次失败：${failure_count} 条未按预期类别失败（详见 ${RAW_LOG_DIR}）")
endif()
message(STATUS "反向用例批次通过（每条都有同环境合法对照、恰好 1 条断言失败且类别被真实断言、两个驱动器自检生效）")
