# P3 运行期 PluginRuntime 校验驱动器（cmake -P）
#
# 判定口径（缺一不可）：
#   1. 两个零插件探针（各自独立进程）必须 exit 0：start 进 Ready、shutdown 回 Idle。
#      · probe-zero-null：records/topo_order/slot_owner/provisions 全为空（头文件明文允许的形态）；
#      · probe-zero-null-records：只把 records 换成非空静态数组，其余同样为空，作为同环境合法对照。
#      两条探针单独成进程运行，是因为该路径曾因 shutdown 内部断言 abort（D-P3-1）而整段丢失结果；
#      修复后仍保留该隔离，作为"关闭路径重新引入断言 abort"的回归哨兵。期望值未放宽。
#      同形态用例已经进入主矩阵（zero.null_view.*），本文件还会逐个点名要求它们真实 PASS。
#   2. 主矩阵 PluginRuntimeCases 退出码必须为 0（任一 Check 失败 -> ExitCode() 置 1 位）；
#   3. 主矩阵 console 里不允许出现 [FAIL]、[SKIP]、invalid-basis 指标行，
#      指标数与 ungraded 计数都必须为 0（本轮不做性能测量）；
#   4. 八个用例组（lifecycle / zero / initfail / register / state / slot / upstream / plan）
#      每组至少 3 条 [PASS]，防止整组被静默删掉；
#   5. 主矩阵 console 与本次刚生成的 out/report.txt 必须逐字一致（同一个 tee），
#      且报告内容必须被本次运行覆盖（用 SHA256 判定，不依赖秒级时间戳）；
#   6. 官方 Benchmark/report.txt 在本次全部运行前后必须完全不变（产物隔离）；
#   7. 主矩阵 stderr 不允许出现 ASSERT FAILED（用例断言不得走断言 abort 路径）；
#   8. 驱动器自检：FAIL 正则必须命中人造 FAIL 行，且不得命中 PASS 行。
#
# 日志布局：raw/runtime-<构建目录名>/ 下保存两条探针与主矩阵的 console、stderr、meta 与索引；
#   同一目录由 batch-owner.txt 绑定单一构建目录，禁止两个构建目录静默混写。
#
# 已知限制：本机只有 GCC 16.2.0 + MinGW Makefiles；MSVC / clang 未验证。
#   本驱动器不做任何性能测量，也不产生评级。

if(NOT RUNTIME_EXE)
    message(FATAL_ERROR "RunRuntimeCases: 缺少 RUNTIME_EXE")
endif()
if(NOT RAW_LOG_DIR)
    message(FATAL_ERROR "RunRuntimeCases: 缺少 RAW_LOG_DIR")
endif()
if(NOT RUNTIME_REPORT)
    message(FATAL_ERROR "RunRuntimeCases: 缺少 RUNTIME_REPORT")
endif()

if(NOT RAW_LOG_MODE)
    set(RAW_LOG_MODE "runtime-unnamed-mode")
endif()
foreach(meta_var BUILD_DIR BATCH_CWD BUILD_TYPE MONOLITHIC GENERATOR CXX_COMPILER CXX_COMPILER_ID OFFICIAL_REPORT)
    if("${${meta_var}}" STREQUAL "")
        set(${meta_var} "unknown")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RAW_LOG_DIR}")
set(index_path "${RAW_LOG_DIR}/runtime-index.txt")
set(owner_path "${RAW_LOG_DIR}/batch-owner.txt")

if(EXISTS "${owner_path}")
    file(STRINGS "${owner_path}" owner_lines)
    if(owner_lines)
        list(GET owner_lines 0 previous_build_dir)
        if(NOT "${previous_build_dir}" STREQUAL "${BUILD_DIR}")
            message(FATAL_ERROR
                "runtime 日志目录 ${RAW_LOG_DIR} 已属于另一个构建目录：\n"
                "  已记录：${previous_build_dir}\n  当前  ：${BUILD_DIR}\n"
                "同一目录不允许被两个构建目录共用。请换构建目录名，或人工确认后删除归属文件。")
        endif()
    endif()
endif()
file(WRITE "${owner_path}" "${BUILD_DIR}\n")

string(TIMESTAMP batch_start "%Y-%m-%d %H:%M:%S")

# ---- 0. 官方报告隔离基线：运行前后必须不变 ----
set(official_before_state "absent")
set(official_before_hash "-")
if(EXISTS "${OFFICIAL_REPORT}")
    file(SHA256 "${OFFICIAL_REPORT}" official_before_hash)
    set(official_before_state "present")
endif()

# ---- 1. 驱动器自检：FAIL 正则必须能命中，且不能被 PASS 行误命中 ----
set(fail_pattern "\\[FAIL\\]")
string(REGEX MATCHALL "${fail_pattern}" selfcheck_fail_hits "  [FAIL] lifecycle.probe_code expect=None actual=Busy")
list(LENGTH selfcheck_fail_hits selfcheck_fail_count)
string(REGEX MATCHALL "${fail_pattern}" selfcheck_pass_hits "  [PASS] lifecycle.probe_code expect=None actual=None")
list(LENGTH selfcheck_pass_hits selfcheck_pass_false_positive)

# ---- 2. 单条探针执行：独立进程 + console/stderr 分开落盘 ----
function(run_runtime_probe case_arg log_stem out_result out_stderr_head)
    set(probe_stdout "${RAW_LOG_DIR}/${log_stem}-stdout.log")
    set(probe_stderr "${RAW_LOG_DIR}/${log_stem}-stderr.log")
    if(EXISTS "${RUNTIME_EXE}")
        execute_process(
            COMMAND "${RUNTIME_EXE}" "${case_arg}"
            WORKING_DIRECTORY "${BATCH_CWD}"
            RESULT_VARIABLE probe_result
            OUTPUT_FILE "${probe_stdout}"
            ERROR_FILE "${probe_stderr}")
    else()
        set(probe_result "missing-exe")
    endif()

    set(stderr_head "-")
    if(EXISTS "${probe_stderr}")
        file(STRINGS "${probe_stderr}" probe_lines LIMIT_COUNT 1)
        if(probe_lines)
            list(GET probe_lines 0 stderr_head)
        endif()
    endif()

    set(${out_result} "${probe_result}" PARENT_SCOPE)
    set(${out_stderr_head} "${stderr_head}" PARENT_SCOPE)
endfunction()

# 参数名与 src/runtime_main.cpp 的分发一一对应
run_runtime_probe("--case-zero-null-shutdown" "probe-zero-null" probe_null_result probe_null_stderr)
run_runtime_probe("--case-zero-null-records-shutdown" "probe-zero-null-records" probe_records_result probe_records_stderr)

# ---- 3. 运行主矩阵（console / stderr 分开落盘） ----
# 探针先跑：它们写的是同一份 out/report.txt，会被主矩阵覆盖，
# 因此最终 out/report.txt 必须对应主矩阵 console
set(stdout_path "${RAW_LOG_DIR}/run-stdout.log")
set(stderr_path "${RAW_LOG_DIR}/run-stderr.log")
set(report_before_time "-")
set(report_before_hash "-")
if(EXISTS "${RUNTIME_REPORT}")
    file(TIMESTAMP "${RUNTIME_REPORT}" report_before_time "%Y-%m-%d %H:%M:%S")
    file(SHA256 "${RUNTIME_REPORT}" report_before_hash)
endif()

set(run_result "missing-exe")
if(EXISTS "${RUNTIME_EXE}")
    execute_process(
        COMMAND "${RUNTIME_EXE}"
        WORKING_DIRECTORY "${BATCH_CWD}"
        RESULT_VARIABLE run_result
        OUTPUT_FILE "${stdout_path}"
        ERROR_FILE "${stderr_path}")
endif()

set(console_text "")
set(stderr_text "")
if(EXISTS "${stdout_path}")
    file(READ "${stdout_path}" console_text)
endif()
if(EXISTS "${stderr_path}")
    file(READ "${stderr_path}" stderr_text)
endif()

string(TIMESTAMP batch_finish "%Y-%m-%d %H:%M:%S")

# ---- 4. 逐项判定 ----
set(problems "")

if(NOT EXISTS "${RUNTIME_EXE}")
    string(APPEND problems "校验程序不存在：${RUNTIME_EXE}\n")
elseif(NOT run_result EQUAL 0)
    string(APPEND problems "主矩阵退出码 ${run_result} != 0（任一 Check 失败即失败）\n")
endif()

# 探针 A：零插件全空视图（D-P3-1 修复后的回归哨兵，不是放宽后的期望）
if(NOT "${probe_null_result}" STREQUAL "0")
    string(APPEND problems
        "零插件全空视图 shutdown 未按头文件契约返回 Idle（探针 exit=${probe_null_result}）——D-P3-1 回归\n"
        "  stderr：${probe_null_stderr}\n"
        "  契约位置：ID/include/Kernel/PluginRuntime.hpp:210-211（零插件允许 records/topo_order/slot_owner/provisions 为空）\n"
        "  断言口径未放宽：期望仍是 start -> Ready、shutdown -> Idle\n")
endif()

# 探针 B：只把 records 换成非空（同环境合法对照）
if(NOT "${probe_records_result}" STREQUAL "0")
    string(APPEND problems
        "零插件 records 非空对照探针失败（探针 exit=${probe_records_result}）\n"
        "  stderr：${probe_records_stderr}\n")
endif()

string(REGEX MATCHALL "${fail_pattern}" fail_hits "${console_text}")
list(LENGTH fail_hits fail_count)
if(NOT fail_count EQUAL 0)
    string(APPEND problems "主矩阵 console 出现 ${fail_count} 条 [FAIL]\n")
endif()

string(REGEX MATCHALL "\\[SKIP\\]" skip_hits "${console_text}")
list(LENGTH skip_hits skip_count)
if(NOT skip_count EQUAL 0)
    string(APPEND problems "主矩阵 console 出现 ${skip_count} 条 [SKIP]：本域不允许跳过\n")
endif()

# 本轮不做性能测量：指标行数必须为 0，六档计数里的 ungraded 也必须为 0
# （不用裸 ungraded 做匹配：SUMMARY 的等级计数行本来就含 "ungraded="）
string(REGEX MATCHALL "\\[invalid-basis\\]" invalid_basis_hits "${console_text}")
list(LENGTH invalid_basis_hits invalid_basis_count)
if(NOT invalid_basis_count EQUAL 0)
    string(APPEND problems "主矩阵 console 出现 ${invalid_basis_count} 条 invalid-basis 指标行\n")
endif()

set(metric_count "-")
if("${console_text}" MATCHES "performance : [0-9]+ domains / ([0-9]+) metrics")
    set(metric_count ${CMAKE_MATCH_1})
endif()
if(NOT metric_count STREQUAL "0")
    string(APPEND problems "主矩阵指标数 metric_count=${metric_count} != 0：本轮不做性能测量\n")
endif()

set(ungraded_grade "-")
if("${console_text}" MATCHES "ungraded=([0-9]+)")
    set(ungraded_grade ${CMAKE_MATCH_1})
endif()
if(NOT ungraded_grade STREQUAL "0")
    string(APPEND problems "主矩阵 ungraded 计数=${ungraded_grade} != 0\n")
endif()

string(FIND "${stderr_text}" "ASSERT FAILED" assert_hit)
if(NOT assert_hit EQUAL -1)
    string(APPEND problems "主矩阵 stderr 出现 ASSERT FAILED：用例触发了断言 abort 路径\n")
endif()

# 每组 PASS 计数：任何一组为 0 或过少都说明覆盖被删减
set(group_prefixes lifecycle zero initfail register state slot upstream plan provision)
set(group_rows "")
set(group_total 0)
foreach(prefix IN LISTS group_prefixes)
    string(REGEX MATCHALL "\\[PASS\\] ${prefix}\\." group_hits "${console_text}")
    list(LENGTH group_hits group_count)
    math(EXPR group_total "${group_total} + ${group_count}")
    string(APPEND group_rows "${prefix}=${group_count} ")
    if(group_count LESS 3)
        string(APPEND problems "用例组 ${prefix} 只有 ${group_count} 条 PASS（要求 >= 3）\n")
    endif()
endforeach()

# 本轮新增/修复的关键用例必须真实出现在主矩阵里（只数总量会被"换一批用例"糊弄过去）
foreach(required_case IN ITEMS
        "zero.null_view_shutdown_ok"
        "zero.null_view_idle"
        "provision.noslot_ready"
        "provision.noslot_idle"
        "provision.mixed_ready"
        "provision.mixed_slot2_published"
        "provision.minus_two_slot.code"
        "provision.noslot_owner_without_slot.code")
    string(FIND "${console_text}" "[PASS] ${required_case}" required_hit)
    if(required_hit EQUAL -1)
        string(APPEND problems "主矩阵缺少必须真实通过的用例：${required_case}\n")
    endif()
endforeach()

# result 行与整体计数
set(result_pass "-")
set(result_fail "-")
set(result_skip "-")
set(result_ok FALSE)
if("${console_text}" MATCHES "result: ([0-9]+) passed / ([0-9]+) failed / ([0-9]+) skipped")
    set(result_pass ${CMAKE_MATCH_1})
    set(result_fail ${CMAKE_MATCH_2})
    set(result_skip ${CMAKE_MATCH_3})
    set(result_ok TRUE)
endif()
if(NOT result_ok)
    string(APPEND problems "主矩阵 console 缺少 result 汇总行\n")
else()
    if(NOT result_fail EQUAL 0)
        string(APPEND problems "result 行 failed=${result_fail} != 0\n")
    endif()
    if(NOT result_skip EQUAL 0)
        string(APPEND problems "result 行 skipped=${result_skip} != 0\n")
    endif()
    if(result_pass LESS 60)
        string(APPEND problems "result 行 passed=${result_pass} < 60：用例数量明显少于本轮矩阵\n")
    endif()
endif()

set(summary_exit "-")
set(summary_ok FALSE)
if("${console_text}" MATCHES "exit code   : ([0-9]+)")
    set(summary_exit ${CMAKE_MATCH_1})
    set(summary_ok TRUE)
endif()
if(NOT summary_ok)
    string(APPEND problems "主矩阵 console 缺少 SUMMARY 的 exit code 行\n")
elseif(NOT summary_exit EQUAL 0)
    string(APPEND problems "SUMMARY exit code=${summary_exit} != 0\n")
endif()

# 报告新鲜度与逐字一致
set(report_state "missing")
set(text_equal FALSE)
set(report_time "-")
set(report_after_hash "-")
if(EXISTS "${RUNTIME_REPORT}")
    set(report_state "present")
    file(TIMESTAMP "${RUNTIME_REPORT}" report_time "%Y-%m-%d %H:%M:%S")
    file(SHA256 "${RUNTIME_REPORT}" report_after_hash)
    file(READ "${RUNTIME_REPORT}" report_text)
    if("${console_text}" STREQUAL "${report_text}")
        set(text_equal TRUE)
    endif()
endif()
if(NOT report_state STREQUAL "present")
    string(APPEND problems "本次报告不存在：${RUNTIME_REPORT}\n")
else()
    if(NOT text_equal)
        string(APPEND problems "主矩阵 console 与 report.txt 不一致（同一个 tee 必须逐字相同）\n")
    endif()
    # 用内容哈希判定覆盖写：探针与主矩阵可能落在同一秒，秒级时间戳不足以证明"被重写过"
    if("${report_after_hash}" STREQUAL "${report_before_hash}")
        string(APPEND problems "report.txt 内容哈希与本次运行前相同（${report_after_hash}）：本次没有覆盖写报告\n")
    endif()
endif()

# 官方报告隔离
set(official_after_state "absent")
set(official_after_hash "-")
if(EXISTS "${OFFICIAL_REPORT}")
    file(SHA256 "${OFFICIAL_REPORT}" official_after_hash)
    set(official_after_state "present")
endif()
if(NOT "${official_before_hash}" STREQUAL "${official_after_hash}")
    string(APPEND problems "官方 ${OFFICIAL_REPORT} 在本次运行中被改动（产物隔离失效）\n")
endif()

# 驱动器自检结论
if(NOT selfcheck_fail_count EQUAL 1)
    string(APPEND problems "驱动器自检失败：FAIL 正则没有命中人造 FAIL 行（${selfcheck_fail_count}）\n")
endif()
if(NOT selfcheck_pass_false_positive EQUAL 0)
    string(APPEND problems "驱动器自检失败：FAIL 正则误命中 PASS 行（${selfcheck_pass_false_positive}）\n")
endif()

# ---- 5. 落盘：meta / 索引 / 摘要 ----
file(WRITE "${RAW_LOG_DIR}/run-meta.txt"
    "MODE            : ${RAW_LOG_MODE}\n"
    "BUILD_DIR       : ${BUILD_DIR}\n"
    "BATCH_CWD       : ${BATCH_CWD}\n"
    "BUILD_TYPE      : ${BUILD_TYPE}\n"
    "MONOLITHIC      : ${MONOLITHIC}\n"
    "GENERATOR       : ${GENERATOR}\n"
    "CXX             : ${CXX_COMPILER}\n"
    "CXX_ID          : ${CXX_COMPILER_ID}\n"
    "DRIVER          : ${CMAKE_CURRENT_LIST_FILE}\n"
    "EXE             : ${RUNTIME_EXE}\n"
    "REPORT          : ${RUNTIME_REPORT}\n"
    "OFFICIAL_REPORT : ${OFFICIAL_REPORT}\n"
    "STARTED         : ${batch_start}\n"
    "FINISHED        : ${batch_finish}\n"
    "PROBE_NULL_EXIT          : ${probe_null_result}\n"
    "PROBE_NULL_RECORDS_EXIT  : ${probe_records_result}\n"
    "MATRIX_EXIT     : ${run_result}\n")

file(WRITE "${index_path}"
    "# P3 运行期 PluginRuntime 用例批次索引（RunRuntimeCases.cmake 自动写入）\n"
    "# 判定：两条探针 exit == 0 && 主矩阵 exit == 0 && [FAIL] == 0 && [SKIP] == 0\n"
    "#       && 每组 PASS >= 3 && 主矩阵 console == out/report.txt 且 report 被覆盖写\n"
    "#       && 官方 Benchmark/report.txt 前后 hash 不变 && 驱动器自检通过\n"
    "MODE            : ${RAW_LOG_MODE}\n"
    "BUILD_DIR       : ${BUILD_DIR}\n"
    "BUILD_TYPE      : ${BUILD_TYPE}\n"
    "STARTED         : ${batch_start}\n"
    "FINISHED        : ${batch_finish}\n"
    "PROBE           : zero_null_shutdown|exit=${probe_null_result}|logs=probe-zero-null-stdout.log,probe-zero-null-stderr.log\n"
    "PROBE           : zero_null_records_shutdown|exit=${probe_records_result}|logs=probe-zero-null-records-stdout.log,probe-zero-null-records-stderr.log\n"
    "MATRIX_EXIT     : ${run_result}\n"
    "PASSED          : ${result_pass}\n"
    "FAILED          : ${fail_count}\n"
    "SKIPPED         : ${skip_count}\n"
    "GROUPS          : ${group_rows}\n"
    "GROUP_TOTAL     : ${group_total}\n"
    "CONSOLE_EQUALS_REPORT : ${text_equal}\n"
    "REPORT_TIME     : ${report_time}\n"
    "REPORT_HASH_BEFORE : ${report_before_hash}\n"
    "REPORT_HASH_AFTER  : ${report_after_hash}\n"
    "METRICS         : ${metric_count}\n"
    "UNGRADED        : ${ungraded_grade}\n"
    "OFFICIAL_REPORT_BEFORE : ${official_before_state} ${official_before_hash}\n"
    "OFFICIAL_REPORT_AFTER  : ${official_after_state} ${official_after_hash}\n"
    "SELFCHECK_FAIL_REGEX   : fail_hits=${selfcheck_fail_count} pass_false_positive=${selfcheck_pass_false_positive}\n"
    "MATRIX_STDERR_ASSERT   : ${assert_hit}\n"
    "LOGS            : run-stdout.log run-stderr.log run-meta.txt\n")

if(problems)
    message(STATUS "runtime 批次问题：\n${problems}")
    message(STATUS "主矩阵摘要：passed=${result_pass} failed=${fail_count} skipped=${skip_count} groups=${group_rows}")
    message(FATAL_ERROR "runtime 校验批次未通过（详见 ${RAW_LOG_DIR}）")
endif()
message(STATUS
    "runtime 校验批次通过：两条零插件探针 exit 0，主矩阵 exit 0，passed=${result_pass}，failed=0，skipped=0，"
    "groups=${group_rows}，console==report.txt")
