# Math negative compile validation driver (invoked by Benchmark/validation/math/CMakeLists.txt via cmake -P)
#
# Criteria (benchmark skill section 6 rule 12 + references/methodology.md part 3 section 4):
#   1. Compile the legal control first: same compiler / flags / includes / identity macros must succeed.
#   2. Compile each "expected to fail" case: it MUST fail, and the diagnostics MUST contain exactly one
#      static assertion, exactly one EXPECTED:<category> matching the registered category, and zero MISMATCH.
#   3. Never accept "non-zero exit code" or "error line count" alone as evidence.
#   4. Write only this asset's own out directory; never touch the official report.txt and never grade.

foreach(required IN ITEMS MATH_VALIDATION_COMPILER MATH_VALIDATION_COMPILER_ID MATH_VALIDATION_DIR
        MATH_VALIDATION_OUT MATH_VALIDATION_WORK MATH_VALIDATION_TOKEN MATH_VALIDATION_FLAGS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "run_negative.cmake missing required parameter: ${required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${MATH_VALIDATION_OUT}" "${MATH_VALIDATION_WORK}")

set(summary_path "${MATH_VALIDATION_OUT}/math-negative-summary-${MATH_VALIDATION_TOKEN}.txt")
set(summary_lines "")
set(failure_count 0)

# Count regex matches (used for "exactly one static assertion" and "exactly one EXPECTED")
function(count_matches OUT_VAR PATTERN TEXT)
    string(REGEX MATCHALL "${PATTERN}" hits "${TEXT}")
    list(LENGTH hits count)
    set(${OUT_VAR} ${count} PARENT_SCOPE)
endfunction()

function(append_summary LINE)
    set(current ${summary_lines})
    list(APPEND current "${LINE}")
    set(summary_lines "${current}" PARENT_SCOPE)
endfunction()

function(build_object_args SOURCE_PATH OUT_VAR)
    # 对象文件名只取源文件名主干：把完整路径当文件名会让编译器无法打开输出文件
    get_filename_component(source_name "${SOURCE_PATH}" NAME_WE)
    set(object "${MATH_VALIDATION_WORK}/${source_name}.o")
    if(MATH_VALIDATION_COMPILER_ID STREQUAL "MSVC")
        set(${OUT_VAR} "/Fo${object}" PARENT_SCOPE)
    else()
        set(${OUT_VAR} -o "${object}" PARENT_SCOPE)
    endif()
endfunction()

function(run_compile SOURCE DIAGNOSTICS_VAR RESULT_VAR)
    build_object_args("${SOURCE}" object_args)
    execute_process(
        COMMAND ${MATH_VALIDATION_COMPILER} ${MATH_VALIDATION_FLAGS} "${SOURCE}" ${object_args}
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr)
    set(${DIAGNOSTICS_VAR} "${compile_stdout}\n${compile_stderr}" PARENT_SCOPE)
    set(${RESULT_VAR} "${compile_result}" PARENT_SCOPE)
endfunction()

append_summary("=== math negative compile validation (${MATH_VALIDATION_TOKEN}) ===")
append_summary("compiler   : ${MATH_VALIDATION_COMPILER}")
append_summary("compiler_id: ${MATH_VALIDATION_COMPILER_ID}")
append_summary("flags      : ${MATH_VALIDATION_FLAGS}")
append_summary("")

# ---------------- 1. legal control: must compile ----------------
foreach(entry IN LISTS MATH_VALIDATION_LEGAL)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 source_name)
    list(GET parts 1 label)
    set(source "${MATH_VALIDATION_DIR}/cases/${source_name}")

    run_compile("${source}" diagnostics compile_result)
    if(compile_result EQUAL 0)
        append_summary("[PASS] legal:${label} compiles (legal control in the same environment)")
    else()
        append_summary("[FAIL] legal:${label} legal control failed to compile (rc=${compile_result})")
        append_summary("---- diagnostics begin ----")
        append_summary("${diagnostics}")
        append_summary("---- diagnostics end ----")
        math(EXPR failure_count "${failure_count} + 1")
    endif()
endforeach()

# ---------------- 2. negative cases: must fail with the registered marker ----------------
foreach(entry IN LISTS MATH_VALIDATION_CASES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 source_name)
    list(GET parts 1 expected_category)
    set(source "${MATH_VALIDATION_DIR}/cases/${source_name}")

    run_compile("${source}" diagnostics compile_result)

    if(compile_result EQUAL 0)
        append_summary("[FAIL] negative:${source_name} compiled successfully but must fail (expected ${expected_category})")
        math(EXPR failure_count "${failure_count} + 1")
        continue()
    endif()

    # 只统计"诊断行"：编译器的源码回显行里也会出现 EXPECTED 字面量，不能算作第二条诊断。
    # GCC/Clang 形态："error: static assertion failed: EXPECTED:<cat>"
    # MSVC 形态    ："error C2338: static_assert failed: 'EXPECTED:<cat>'"
    count_matches(assertion_count "static assertion failed|static_assert failed" "${diagnostics}")
    # 逐行计数：编译器的源码回显行同样含有 EXPECTED 字面量，只能统计带 "error" 的诊断行
    string(REPLACE "\n" ";" diagnostic_lines "${diagnostics}")
    set(marker_lines 0)
    set(mismatch_count 0)
    foreach(line IN LISTS diagnostic_lines)
        if(line MATCHES "error.*EXPECTED:${expected_category}")
            math(EXPR marker_lines "${marker_lines} + 1")
        endif()
        if(line MATCHES "MISMATCH")
            math(EXPR mismatch_count "${mismatch_count} + 1")
        endif()
    endforeach()

    if(NOT assertion_count EQUAL 1)
        append_summary("[FAIL] negative:${source_name} static assertion count ${assertion_count} (must be exactly 1)")
        math(EXPR failure_count "${failure_count} + 1")
        continue()
    endif()
    if(NOT marker_lines EQUAL 1)
        append_summary("[FAIL] negative:${source_name} EXPECTED:${expected_category} appears in ${marker_lines} error line(s) (must be exactly 1)")
        math(EXPR failure_count "${failure_count} + 1")
        continue()
    endif()
    if(NOT mismatch_count EQUAL 0)
        append_summary("[FAIL] negative:${source_name} diagnostics contain MISMATCH (${mismatch_count}): the failure comes from a wrong guard, not from the registered EXPECTED assertion")
        math(EXPR failure_count "${failure_count} + 1")
        continue()
    endif()

    append_summary("[PASS] negative:${source_name} exactly 1 static assertion + EXPECTED:${expected_category}")
endforeach()

# ---------------- 3. summary ----------------
append_summary("")
if(failure_count EQUAL 0)
    append_summary("RESULT: all math negative compile cases passed")
else()
    append_summary("RESULT: ${failure_count} case(s) failed")
endif()

string(REPLACE ";" "\n" summary_text "${summary_lines}")
file(WRITE "${summary_path}" "${summary_text}\n")
message(STATUS "math negative validation (${MATH_VALIDATION_TOKEN}) summary: ${summary_path}")
message(STATUS "${summary_text}")

if(NOT failure_count EQUAL 0)
    message(FATAL_ERROR "math negative compile validation failed: ${failure_count} case(s), see ${summary_path}")
endif()
