# 组装 fixture 矩阵入口包装（cmake -P）
#
# 职责：把 validation/assembly 的目标命令与 RunFixtureMatrix.cmake 之间的一层"外部观察点"具象化。
#
#   1. 用 execute_process 调用 cmake/RunFixtureMatrix.cmake，捕获它的**外部观察退出码**（driver_rc）；
#   2. 把这个退出码回填进报告头部的 `driver exit code : PENDING_EXTERNAL_OBSERVATION` 占位行，
#      并另写 out/fixture-matrix-driver-exit.txt 作为独立的机器可读证据；
#   3. 以同一退出码失败，让外层 `cmake --build ... --target IDAssemblyFixtureMatrixRun`
#      的外部退出码与驱动器判定一致（两者不一致时报告里的两行会互相矛盾，一眼可见）。
#
# 为什么需要这一层：报告是驱动器**自己写的**，它无法知道"自己被外部看到时返回了什么"。
# 直接写"我通过所以退出码是 0"属于自证；只有外层捕获值才是外部观测证据（计划书 §9.7 / §9.8）。
#
# 参数（由 validation/assembly/CMakeLists.txt 透传）：
#   ASSEMBLY_DIR     —— Benchmark/validation/assembly 目录
#   REPORT_DIR       —— 报告目录（<build>/Benchmark/validation/assembly/out）
#   IDASM_DRIVER_ARGS—— 透传给内层驱动器的 -D<k>=<v> 列表（逐项由本脚本原样转发）
#
# 退出码：与内层驱动器一致（0 = 全部 case 按期望通过；1 = 至少一条判定不符）。

foreach(required_var ASSEMBLY_DIR REPORT_DIR IDASM_DRIVER_ARGS)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "RunFixtureMatrixEntry: 缺少参数 ${required_var}")
    endif()
endforeach()

set(inner_script "${ASSEMBLY_DIR}/cmake/RunFixtureMatrix.cmake")
if(NOT EXISTS "${inner_script}")
    message(FATAL_ERROR "RunFixtureMatrixEntry: 找不到内层驱动器 ${inner_script}")
endif()

message(STATUS "fixture-matrix[entry] 内层驱动器：${inner_script}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${IDASM_DRIVER_ARGS} -P "${inner_script}"
    RESULT_VARIABLE driver_rc)

string(TIMESTAMP observed_at "%Y-%m-%d %H:%M:%S")
set(report_path "${REPORT_DIR}/fixture-matrix-report.txt")

# 独立证据：不依赖报告正文也能核对外部退出码
file(WRITE "${REPORT_DIR}/fixture-matrix-driver-exit.txt"
    "# 组装 fixture 矩阵驱动器外部退出码（RunFixtureMatrixEntry.cmake 捕获，非驱动器自述）\n"
    "observed_exit=${driver_rc}\n"
    "observed_at=${observed_at}\n"
    "inner_script=${inner_script}\n"
    "report=${report_path}\n")

# 回填报告头占位行：只替换唯一 token，避免碰到正文其它文字
if(EXISTS "${report_path}")
    file(READ "${report_path}" report_text)
    string(FIND "${report_text}" "PENDING_EXTERNAL_OBSERVATION" placeholder_pos)
    if(placeholder_pos LESS 0)
        message(STATUS "fixture-matrix[entry] 报告里没有占位 token（可能内层驱动器提前失败），不回填正文")
    else()
        string(REPLACE "PENDING_EXTERNAL_OBSERVATION"
            "${driver_rc}（RunFixtureMatrixEntry.cmake 于 ${observed_at} 捕获的外部 cmake -P 退出码）"
            report_text "${report_text}")
        file(WRITE "${report_path}" "${report_text}")
        message(STATUS "fixture-matrix[entry] 已回填 driver exit code=${driver_rc} 到 ${report_path}")
    endif()
else()
    message(STATUS "fixture-matrix[entry] 报告不存在（${report_path}）：内层驱动器未产出报告")
endif()

if(NOT driver_rc EQUAL 0)
    message(FATAL_ERROR
        "组装 fixture 矩阵未通过：内层驱动器外部退出码=${driver_rc}（详见 ${report_path}）")
endif()

message(STATUS "组装 fixture 矩阵通过：内层驱动器外部退出码=${driver_rc}（报告头已回填同一值）")
