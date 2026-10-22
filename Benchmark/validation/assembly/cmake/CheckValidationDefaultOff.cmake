# 组装校验资产 —— 默认 OFF 回归（cmake -P）
#
# 职责：证明 `ID_BUILD_VALIDATION_ASSEMBLY` 保持默认 OFF 时，
#   1. 四个配置（MinGW/MSVC × Debug/Release）各自 configure 退出码为 0；
#   2. 该构建目录里**不存在任何 IDAssembly* 目标/产物痕迹**（本资产默认不参与构建）。
#
# 判定器自检：先用**已开启**本资产（ID_BUILD_VALIDATION_ASSEMBLY=ON）的构建目录做正向对照——
# 同一个扫描器必须在**每一个** control 目录里检出 IDAssembly* 目标痕迹。检不出来说明扫描器对
# 该生成器失效，此时"OFF 目录没有 IDAssembly*"就是假阴性，不允许当成通过。
# （两种生成器的目标痕迹形式不同：VS 有 .vcxproj/.rule 文件，Makefile 生成器只有
#  CMakeFiles/TargetDirectories.txt 里的目标目录条目，所以 control 必须覆盖两种生成器。）
#
# 参数：
#   BENCHMARK_DIR      —— <engine>/Benchmark
#   OUT_DIR            —— 报告与 configure 日志目录（构建目录内的 out）
#   CONTROL_BUILD_DIRS —— 已开启本资产的构建目录列表（建议同时给 MinGW 与 MSVC 各一个）
#   VARIANTS           —— 每项 `label|build_dir|generator|platform|instance|build_type`，分号分隔多项
#
# 退出码：0 = 全部 control 自检检出 + 四格 configure 0 + 四格无 IDAssembly*；1 = 任一不符。

foreach(required_var BENCHMARK_DIR OUT_DIR CONTROL_BUILD_DIRS VARIANTS)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "CheckValidationDefaultOff: 缺少参数 ${required_var}")
    endif()
endforeach()

get_filename_component(engine_source_dir "${BENCHMARK_DIR}/.." ABSOLUTE)
file(MAKE_DIRECTORY "${OUT_DIR}")

set(problems "")

# 扫描一个构建目录里的 IDAssembly* 目标痕迹，返回"证据行"列表（每行是一处痕迹）。
#
# 两个信号互补，缺一个就会漏：
#   (a) 生成物/目标文件：文件名含 IDAssembly（VS 的 .vcxproj / .rule / .recipe / .lastbuildstate 等；
#       Makefile 生成器的自定义目标在从未构建时可能没有同名文件，所以必须配 (b)）；
#   (b) CMakeFiles/TargetDirectories.txt：两种生成器都把每个目标目录写进这个文件，
#       自定义目标（IDAssemblyValidationRun / IDAssemblyFixtureMatrixRun …）一定出现在里面。
#
# 刻意不用 GLOB_RECURSE + LIST_DIRECTORIES true 做目录扫描：实测该组合会把目录树上
# **所有**目录都返回（模式对目录项不生效），会把干净的 OFF 目录误报成 45 条命中。
function(default_off_scan out_list build_dir)
    set(hits "")
    if(NOT EXISTS "${build_dir}")
        set(${out_list} "" PARENT_SCOPE)
        return()
    endif()

    file(GLOB_RECURSE name_hits "${build_dir}/*IDAssembly*")
    foreach(hit IN LISTS name_hits)
        list(APPEND hits "${hit}")
    endforeach()

    if(EXISTS "${build_dir}/CMakeFiles/TargetDirectories.txt")
        file(READ "${build_dir}/CMakeFiles/TargetDirectories.txt" target_dirs_text)
        string(REGEX MATCHALL "[^\r\n]*IDAssembly[^\r\n]*" idassembly_lines "${target_dirs_text}")
        foreach(line IN LISTS idassembly_lines)
            string(STRIP "${line}" line)
            if(NOT "${line}" STREQUAL "")
                list(APPEND hits "TargetDirectories.txt: ${line}")
            endif()
        endforeach()
    endif()

    set(${out_list} "${hits}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# 0. 判定器自检：每个 ON 控制目录都必须被检出
# ---------------------------------------------------------------------------
# 用 string(CONCAT) 而不是 set(a "b" "c")：后者会生成带分号的列表，把报告行拼成 "a;b"
string(CONCAT selfcheck_report "")
set(control_index 0)
foreach(control_dir IN LISTS CONTROL_BUILD_DIRS)
    default_off_scan(control_hits "${control_dir}")
    list(LENGTH control_hits control_hit_count)
    string(APPEND selfcheck_report
        "  control[${control_index}]      : ${control_dir}\n"
        "    hits            : ${control_hit_count}\n")
    if(control_hit_count LESS 1)
        string(APPEND selfcheck_report "    result          : UNEXPECTED（该控制目录里检不到 IDAssembly*，扫描器对该生成器失效）\n")
        string(APPEND problems "判定器自检失败：控制目录 ${control_dir} 检不到任何 IDAssembly*，OFF 结论不可信\n")
    else()
        string(APPEND selfcheck_report "    result          : as-expected（检出 ${control_hit_count} 条 IDAssembly*，扫描器有效）\n")
        # 只摘录少量样本，避免报告被目录清单淹没
        set(sample_count 0)
        foreach(hit IN LISTS control_hits)
            if(sample_count LESS 2)
                string(APPEND selfcheck_report "    sample          : ${hit}\n")
                math(EXPR sample_count "${sample_count} + 1")
            endif()
        endforeach()
    endif()
    math(EXPR control_index "${control_index} + 1")
endforeach()

# ---------------------------------------------------------------------------
# 1. 逐配置 configure（开关默认 OFF）并扫描
# ---------------------------------------------------------------------------
set(variant_report "")
set(variant_index 0)
foreach(spec IN LISTS VARIANTS)
    if(spec STREQUAL "")
        continue()
    endif()
    string(REPLACE "|" ";" spec_fields "${spec}")
    list(LENGTH spec_fields spec_len)
    if(NOT spec_len EQUAL 6)
        message(FATAL_ERROR "CheckValidationDefaultOff: VARIANTS 每项必须是 label|build_dir|generator|platform|instance|build_type，当前：${spec}")
    endif()
    list(GET spec_fields 0 label)
    list(GET spec_fields 1 build_dir)
    list(GET spec_fields 2 generator)
    list(GET spec_fields 3 platform)
    list(GET spec_fields 4 instance)
    list(GET spec_fields 5 build_type)

    # n/a = 该字段对当前生成器不适用（单配置生成器没有 -A / instance；多配置生成器不认 CMAKE_BUILD_TYPE）
    set(configure_args -S "${engine_source_dir}" -B "${build_dir}")
    if(NOT "${generator}" STREQUAL "" AND NOT "${generator}" STREQUAL "n/a")
        list(APPEND configure_args -G "${generator}")
    endif()
    if(NOT "${platform}" STREQUAL "" AND NOT "${platform}" STREQUAL "n/a")
        list(APPEND configure_args -A "${platform}")
    endif()
    if(NOT "${instance}" STREQUAL "" AND NOT "${instance}" STREQUAL "n/a")
        list(APPEND configure_args "-DCMAKE_GENERATOR_INSTANCE=${instance}")
    endif()
    if(NOT "${build_type}" STREQUAL "" AND NOT "${build_type}" STREQUAL "n/a")
        list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${build_type}")
    endif()
    list(APPEND configure_args
        "-DID_BUILD_VALIDATION_ASSEMBLY=OFF"
        "-DID_ASSEMBLY_PROFILE="
        "-DID_BUILD_SANDBOX=OFF")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${configure_args}
        WORKING_DIRECTORY "${engine_source_dir}"
        RESULT_VARIABLE configure_rc
        OUTPUT_FILE "${OUT_DIR}/default-off-configure-${label}.out"
        ERROR_FILE  "${OUT_DIR}/default-off-configure-${label}.err")

    # 开关取值必须真的是 OFF（不是"文件不存在所以假定 OFF"）
    set(cache_switch "missing")
    set(cache_profile "missing")
    if(EXISTS "${build_dir}/CMakeCache.txt")
        file(READ "${build_dir}/CMakeCache.txt" cache_text)
        if(cache_text MATCHES "ID_BUILD_VALIDATION_ASSEMBLY:BOOL=([A-Za-z0-9]+)")
            set(cache_switch "${CMAKE_MATCH_1}")
        endif()
        if(cache_text MATCHES "ID_ASSEMBLY_PROFILE:STRING=([^\r\n]*)")
            string(STRIP "${CMAKE_MATCH_1}" cache_profile)
            if("${cache_profile}" STREQUAL "")
                set(cache_profile "<empty>")
            endif()
        endif()
    endif()

    default_off_scan(variant_hits "${build_dir}")
    list(LENGTH variant_hits variant_hit_count)
    set(assemble_dir_state "absent")
    if(EXISTS "${build_dir}/Assemble")
        set(assemble_dir_state "PRESENT")
    endif()

    set(variant_verdict "ok")
    if(NOT configure_rc EQUAL 0)
        set(variant_verdict "configure-failed")
        string(APPEND problems "[${label}] configure 退出码=${configure_rc}（期望 0，见 ${OUT_DIR}/default-off-configure-${label}.err）\n")
    endif()
    if(NOT cache_switch STREQUAL "OFF")
        set(variant_verdict "switch-not-off")
        string(APPEND problems "[${label}] CMakeCache 里 ID_BUILD_VALIDATION_ASSEMBLY=${cache_switch}（期望 OFF）\n")
    endif()
    if(NOT variant_hit_count EQUAL 0)
        set(variant_verdict "idassembly-targets-present")
        string(APPEND problems "[${label}] 默认 OFF 构建目录里仍有 ${variant_hit_count} 条 IDAssembly* 路径（期望 0）\n")
    endif()
    if(NOT assemble_dir_state STREQUAL "absent")
        set(variant_verdict "assemble-dir-present")
        string(APPEND problems "[${label}] 空 profile 却生成了 Assemble/ 子目录（期望不存在）\n")
    endif()

    string(APPEND variant_report
        "  ${label}\n"
        "    build dir        : ${build_dir}\n"
        "    generator        : ${generator} ${platform} ${instance}\n"
        "    build type       : ${build_type}\n"
        "    configure exit   : ${configure_rc}\n"
        "    cache switch     : ID_BUILD_VALIDATION_ASSEMBLY=${cache_switch}\n"
        "    cache profile    : ID_ASSEMBLY_PROFILE=${cache_profile}\n"
        "    IDAssembly* hits : ${variant_hit_count}\n"
        "    Assemble/ dir    : ${assemble_dir_state}\n"
        "    verdict          : ${variant_verdict}\n")
    math(EXPR variant_index "${variant_index} + 1")
endforeach()

if(problems)
    set(exit_code 1)
    set(problem_text "${problems}")
else()
    set(exit_code 0)
    set(problem_text "无（判定器自检检出；四格 configure 退出码 0、开关 OFF、零 IDAssembly* 目标）\n")
endif()

string(TIMESTAMP run_started "%Y-%m-%d %H:%M:%S")
set(report_path "${OUT_DIR}/validation-default-off.txt")

string(CONCAT report_text
    "=== Benchmark/validation/assembly — 默认 OFF 回归（ID_BUILD_VALIDATION_ASSEMBLY=OFF）===\n"
    "generated        : ${run_started}\n"
    "tool             : ${CMAKE_CURRENT_LIST_FILE}\n"
    "scope            : 只证明「开关默认 OFF 时四个配置都能 configure 成功，且不产生 IDAssembly* 目标」；\n"
    "                   不构建、不运行、不做性能测量、不写官方 report.txt、不产生评级\n"
    "engine source    : ${engine_source_dir}\n"
    "out dir          : ${OUT_DIR}\n"
    "variants         : ${variant_index}\n"
    "--------------------------------------------------------------------\n"
    "[0] 判定器自检（同一扫描器必须在每个「已开启本资产」的生成器构建目录里检出 IDAssembly*）\n"
    "${selfcheck_report}"
    "--------------------------------------------------------------------\n"
    "[1] 四配置 configure（开关 OFF）与目标扫描\n"
    "${variant_report}"
    "--------------------------------------------------------------------\n"
    "[2] 口径与局限\n"
    "  - configure 退出码是**外部进程观察值**（execute_process RESULT_VARIABLE）；不是读日志猜的。\n"
    "  - 「不生成 IDAssembly* 目标」的判据是两条互补信号都为 0：① 构建目录里名字含 IDAssembly 的文件；\n"
    "    ② CMakeFiles/TargetDirectories.txt 里的 IDAssembly 目标目录条目。并且空 profile 时不出现\n"
    "    <build>/Assemble/ 子目录（根 CMakeLists 只在 profile 非空时 add_subdirectory）。\n"
    "    单用 ① 会漏掉从未构建过的 Makefile 自定义目标，所以必须有 ②；判定器自检用「已开启本资产」的\n"
    "    MinGW 与 MSVC 构建目录各一份做正向对照，两边都必须检出 ≥1 条痕迹才算扫描器有效。\n"
    "  - 本回归不构建任何目标，因此不证明「目标存在则一定可用」；目标可用性由 fixture 矩阵与 P2 批次证明。\n"
    "  - MSVC 多配置生成器的一个 build dir 同时承载 Debug/Release：本题按配置各建一个 build dir，\n"
    "    让每格都有自己的 configure 退出码。\n"
    "--------------------------------------------------------------------\n"
    "SUMMARY\n"
    "  variants         : ${variant_index}\n"
    "  problems         : ${problem_text}\n"
    "  exit code        : ${exit_code}\n")

file(WRITE "${report_path}" "${report_text}")

message(STATUS "default-off: variants=${variant_index} -> ${report_path}")
message(STATUS "default-off: exit code=${exit_code}")

if(exit_code EQUAL 0)
    message(STATUS "默认 OFF 回归通过：判定器自检检出，四格 configure 退出码 0 且零 IDAssembly* 目标")
else()
    message(STATUS "默认 OFF 回归发现问题：\n${problems}")
    message(FATAL_ERROR "默认 OFF 回归未通过（详见 ${report_path}）")
endif()
