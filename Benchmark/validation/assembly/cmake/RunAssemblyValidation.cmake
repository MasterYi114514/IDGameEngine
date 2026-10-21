# P2 组装层校验驱动器（cmake -P）
#
# 判定口径（缺一不可）：
#   0. 判定器自检：用临时构造的 stdout 证明 Smoke 点名断言既接受正确输出、也拒绝错误输出；
#   1. 四个变体（SHARED/STATIC × profile=default/zero）都要 configure=0、build=0、Smoke 运行=0，
#      且 Smoke 的实际 stdout 必须与产品侧 4 条断言逐条一致（点名断言，见第 0 节）：
#      start -> Ready、宿主小对象 new/delete 能分配释放、>8KB 宿主 new 被内核大对象记账（grew ≥ 1 MiB）、
#      shutdown -> Idle；缺行/多行/重复/文案改动/数值越界任一出现即失败；
#   2. STATIC 与 SHARED 在 default profile 下的 selected target 集合、身份宏、构建证据行、
#      运行期绑定 TU、产物模块集合必须一致；只有 .a / .dll 这种按模式的产物形态允许不同；
#   3. 未选中插件不得出现在身份宏、链接边、绑定 TU、产物与导出头里（zero profile 是"全部未选中"）；
#   4. 零插件产品：IDAssemblyValidate 与 IDAssembly 能单独编译，start 进 Ready、shutdown 回 Idle；
#   5. 改坏构建证据（选中集合/目标/宏/身份/provider/边/槽/运行期元数据）必须被 C++ 静态校验拒绝：
#      用独立 TU 编译并断言**具体错误类别**，每个用例同时给出"错误类别不对就编译失败"的反向探针，
#      以及同环境的合法对照（同一份生成输入未改坏时两个 TU 都必须编译成功）；
#   6. 切换 profile 不删缓存也能重新配置并更新生成物：用生成物内容哈希/时间戳与运行结果取证；
#   7. 分配桩：whole-archive 只作用于 IDPluginStub（一对标记、里面不含任何插件库）；
#      STATIC 最终程序里每个 new/delete 替换重载只有 1 个定义，SHARED 每个模块各有自己的桩。
#
# 本驱动器只做正确性取证，不做任何性能测量、不新增评级、不改写官方 Benchmark/report.txt。
# 已知限制：本机只有 GCC 16.2.0 + MinGW Makefiles；MSVC / clang / 多配置生成器未验证。

foreach(required_var ENGINE_SOURCE_DIR WORK_DIR RAW_LOG_DIR GENERATOR BUILD_TYPE CXX_COMPILER CXX_COMPILER_ID PROBE_SOURCE)
    if("${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "RunAssemblyValidation: 缺少参数 ${required_var}")
    endif()
endforeach()

if(NOT EXISTS "${ENGINE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "RunAssemblyValidation: ENGINE_SOURCE_DIR 不含 CMakeLists.txt：${ENGINE_SOURCE_DIR}")
endif()
if(NOT EXISTS "${PROBE_SOURCE}")
    message(FATAL_ERROR "RunAssemblyValidation: 找不到探针源文件：${PROBE_SOURCE}")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}" "${RAW_LOG_DIR}")
set(owner_path "${RAW_LOG_DIR}/batch-owner.txt")
if(EXISTS "${owner_path}")
    file(STRINGS "${owner_path}" owner_lines)
    if(owner_lines)
        list(GET owner_lines 0 previous_work_dir)
        if(NOT "${previous_work_dir}" STREQUAL "${WORK_DIR}")
            message(FATAL_ERROR
                "assembly 原始日志目录 ${RAW_LOG_DIR} 已属于另一个工作目录：\n"
                "  已记录：${previous_work_dir}\n  当前  ：${WORK_DIR}\n"
                "同一目录不允许被两个构建目录共用。请换构建目录名，或人工确认后删除归属文件。")
        endif()
    endif()
endif()
file(WRITE "${owner_path}" "${WORK_DIR}\n")

string(TIMESTAMP batch_start "%Y-%m-%d %H:%M:%S")
set(problems "")
set(notes "")

# ---- 通用执行封装 -------------------------------------------------------------
# asm_run(<out_rc> <stdout_log> <command...>)：在 WORK_DIR 下执行，stdout/stderr 分开落盘
function(asm_run out_rc stdout_log)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE run_rc
        OUTPUT_FILE "${stdout_log}"
        ERROR_FILE "${stdout_log}.err")
    set(${out_rc} "${run_rc}" PARENT_SCOPE)
endfunction()

# asm_add_problem(<text>)：把一条问题追加进批次结论（problems 是脚本作用域变量）
function(asm_add_problem text)
    set(problems "${problems}${text}\n" PARENT_SCOPE)
endfunction()

# asm_add_note(<text>)：把一条"未验证/未覆盖"如实记进结论
function(asm_add_note text)
    set(notes "${notes}${text}\n" PARENT_SCOPE)
endfunction()

# 产物名归一化：libX.dll.a -> X:import，libX.dll -> X:runtime，libX.a -> X:archive，X.exe -> X:exe
function(asm_artifact_token out_token file_name)
    set(name "${file_name}")
    string(REGEX REPLACE "^lib" "" name "${name}")
    if(name MATCHES "\\.dll\\.a$")
        string(REGEX REPLACE "\\.dll\\.a$" "" name "${name}")
        set(${out_token} "${name}:import" PARENT_SCOPE)
    elseif(name MATCHES "\\.dll$")
        string(REGEX REPLACE "\\.dll$" "" name "${name}")
        set(${out_token} "${name}:runtime" PARENT_SCOPE)
    elseif(name MATCHES "\\.a$")
        string(REGEX REPLACE "\\.a$" "" name "${name}")
        set(${out_token} "${name}:archive" PARENT_SCOPE)
    elseif(name MATCHES "\\.exe$")
        string(REGEX REPLACE "\\.exe$" "" name "${name}")
        set(${out_token} "${name}:exe" PARENT_SCOPE)
    else()
        set(${out_token} "${name}:other" PARENT_SCOPE)
    endif()
endfunction()

# asm_write_lines(<路径> <行...>)：逐行写文本文件（每个参数补一个换行）
# 注意：file(WRITE) 在 Windows 上会把 "\n" 写成 CRLF（实测 0D 0A），判定函数已统一去掉 CR
function(asm_write_lines path)
    set(text "")
    foreach(line IN LISTS ARGN)
        string(APPEND text "${line}\n")
    endforeach()
    file(WRITE "${path}" "${text}")
endfunction()

# ---------------------------------------------------------------------------
# 0. Smoke 输出点名判定 + 判定器自检
# ---------------------------------------------------------------------------
# 产品侧 Assemble/tests/consumer/AssemblySmoke.cpp 的 4 条断言就是"事实文案"，驱动器逐条点名：
#   start -> Ready、宿主小对象 new/delete 能分配释放、>8KB 宿主 new 被内核大对象记账、
#   shutdown -> Idle。判定按集合相等：每条文案恰好命中一行，且不得出现未点名的行。
# 这里不数 "[PASS]" 出现次数：次数相同不等于语义相同（旧文案变体就会被放过）。
set(ASM_SMOKE_EXPECTED_LINES
    "^\\[PASS\\] start state=Ready$"
    "^\\[PASS\\] host small new/delete completed \\(alloc and free succeeded\\)$"
    "^\\[PASS\\] host large new/delete accounted by kernel stats \\(grew=[0-9]+ bytes, released=1\\)$"
    "^\\[PASS\\] shutdown state=Idle \\(code=None\\)$")

# 大对象行的语义下限：AssemblySmoke.cpp 的 kProofBytes = 1 MiB（小于它只能说明没走大对象记账）
set(ASM_SMOKE_MIN_GROWN 1048576)

# asm_smoke_output_problems(<out_var> <log_path>)：返回换行分隔的问题清单，空串表示逐条一致
function(asm_smoke_output_problems out_var log_path)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT EXISTS "${log_path}")
        set(${out_var} "Smoke 输出日志不存在：${log_path}\n" PARENT_SCOPE)
        return()
    endif()

    # printf 文本模式在 Windows 上写 CRLF：先去 CR 再按行切分，末尾换行不算一行
    file(READ "${log_path}" smoke_raw)
    string(REPLACE "\r" "" smoke_raw "${smoke_raw}")
    string(REGEX REPLACE "\n+$" "" smoke_raw "${smoke_raw}")
    set(smoke_lines "")
    if(NOT smoke_raw STREQUAL "")
        string(REPLACE "\n" ";" smoke_lines "${smoke_raw}")
    endif()

    set(local_problems "")
    list(LENGTH smoke_lines smoke_line_count)
    list(LENGTH ASM_SMOKE_EXPECTED_LINES expected_count)
    if(NOT smoke_line_count EQUAL expected_count)
        string(APPEND local_problems "Smoke 输出 ${smoke_line_count} 行，点名表要求 ${expected_count} 行\n")
    endif()

    # 每条期望文案必须恰好命中 1 行（漏行与重复行都在这里暴露）
    foreach(pattern IN LISTS ASM_SMOKE_EXPECTED_LINES)
        set(hits 0)
        foreach(line IN LISTS smoke_lines)
            if(line MATCHES "${pattern}")
                math(EXPR hits "${hits} + 1")
            endif()
        endforeach()
        if(NOT hits EQUAL 1)
            string(APPEND local_problems "期望文案命中 ${hits} 次（应为 1）：${pattern}\n")
        endif()
    endforeach()

    # 实际输出里不得有未点名的行（多出来的行同样失败）
    foreach(line IN LISTS smoke_lines)
        set(named 0)
        foreach(pattern IN LISTS ASM_SMOKE_EXPECTED_LINES)
            if(line MATCHES "${pattern}")
                math(EXPR named "${named} + 1")
            endif()
        endforeach()
        if(named EQUAL 0)
            string(APPEND local_problems "出现未点名的输出行：${line}\n")
        endif()
    endforeach()

    # 大对象行的事实数值：grew 必须达到 1 MiB，否则"被内核大对象记账"没有被证明
    foreach(line IN LISTS smoke_lines)
        if(line MATCHES "^\\[PASS\\] host large new/delete accounted by kernel stats \\(grew=([0-9]+) bytes, released=1\\)$")
            set(grown_bytes "${CMAKE_MATCH_1}")
            if(grown_bytes LESS ASM_SMOKE_MIN_GROWN)
                string(APPEND local_problems "大对象行 grew=${grown_bytes} 低于 1 MiB 下限 ${ASM_SMOKE_MIN_GROWN}\n")
            endif()
        endif()
    endforeach()

    set(${out_var} "${local_problems}" PARENT_SCOPE)
endfunction()

set(selfcheck_dir "${WORK_DIR}/driver-selfcheck")
file(MAKE_DIRECTORY "${selfcheck_dir}")

# 4 条正确文案：与产品侧当前输出逐字一致（grew 取本轮实测的 1114112 = 1 MiB + 64 KiB 页头）
set(selfcheck_good_lines
    "[PASS] start state=Ready"
    "[PASS] host small new/delete completed (alloc and free succeeded)"
    "[PASS] host large new/delete accounted by kernel stats (grew=1114112 bytes, released=1)"
    "[PASS] shutdown state=Idle (code=None)")

# 对照：正确输出必须被判为无问题
asm_write_lines("${selfcheck_dir}/ok.log" ${selfcheck_good_lines})

# 缺一行：只数 [PASS] 的旧判据在数量调整后仍可能放过，点名断言必须拒绝
set(selfcheck_case_lines ${selfcheck_good_lines})
list(REMOVE_AT selfcheck_case_lines 1)
asm_write_lines("${selfcheck_dir}/missing-line.log" ${selfcheck_case_lines})

# 重复一行：某条文案命中 2 次
set(selfcheck_case_lines ${selfcheck_good_lines})
list(GET selfcheck_good_lines 3 selfcheck_duplicated_line)
list(APPEND selfcheck_case_lines "${selfcheck_duplicated_line}")
asm_write_lines("${selfcheck_dir}/duplicate-line.log" ${selfcheck_case_lines})

# 多一行：一条完全未命名的输出行
set(selfcheck_case_lines ${selfcheck_good_lines})
asm_write_lines("${selfcheck_dir}/extra-line.log" ${selfcheck_case_lines} "[INFO] driver self-check unexpected line")

# 文案被改：状态词不再是 Ready
set(selfcheck_case_lines ${selfcheck_good_lines})
list(GET selfcheck_case_lines 0 selfcheck_changed_line)
string(REPLACE "state=Ready" "state=Initializing" selfcheck_changed_line "${selfcheck_changed_line}")
list(REMOVE_AT selfcheck_case_lines 0)
list(INSERT selfcheck_case_lines 0 "${selfcheck_changed_line}")
asm_write_lines("${selfcheck_dir}/changed-state.log" ${selfcheck_case_lines})

# 语义被改：shutdown 行用 [FAIL] 冒充
set(selfcheck_case_lines ${selfcheck_good_lines})
list(REMOVE_AT selfcheck_case_lines 3)
asm_write_lines("${selfcheck_dir}/fail-not-pass.log" ${selfcheck_case_lines} "[FAIL] shutdown state=Idle (code=None)")

# 数值不达标：大对象 grew 只有 4 KiB
set(selfcheck_case_lines ${selfcheck_good_lines})
list(GET selfcheck_case_lines 2 selfcheck_large_line)
string(REPLACE "grew=1114112" "grew=4096" selfcheck_large_line "${selfcheck_large_line}")
list(REMOVE_AT selfcheck_case_lines 2)
list(INSERT selfcheck_case_lines 2 "${selfcheck_large_line}")
asm_write_lines("${selfcheck_dir}/large-not-routed.log" ${selfcheck_case_lines})

# 逐用例核对"判定器给出的结论是否与期望一致"：ok 必须无问题，其余必须被拒绝
set(selfcheck_cases
    "ok:0"
    "missing-line:1"
    "duplicate-line:1"
    "extra-line:1"
    "changed-state:1"
    "fail-not-pass:1"
    "large-not-routed:1")
set(selfcheck_report "")
foreach(entry IN LISTS selfcheck_cases)
    string(REPLACE ":" ";" selfcheck_fields "${entry}")
    list(GET selfcheck_fields 0 selfcheck_name)
    list(GET selfcheck_fields 1 selfcheck_expect)
    asm_smoke_output_problems(selfcheck_problems "${selfcheck_dir}/${selfcheck_name}.log")
    set(selfcheck_rejected 0)
    if(selfcheck_problems)
        set(selfcheck_rejected 1)
    endif()
    if(selfcheck_rejected EQUAL selfcheck_expect)
        set(selfcheck_state "as-expected")
    else()
        set(selfcheck_state "UNEXPECTED")
        asm_add_problem("[driver-selfcheck] ${selfcheck_name}：期望 rejected=${selfcheck_expect}，实际 rejected=${selfcheck_rejected}")
    endif()
    string(REPLACE "\n" " | " selfcheck_text "${selfcheck_problems}")
    string(APPEND selfcheck_report
        "${selfcheck_name}: rejected=${selfcheck_rejected} expect=${selfcheck_expect} ${selfcheck_state} ${selfcheck_text}\n")
endforeach()
file(WRITE "${WORK_DIR}/driver-selfcheck-summary.txt" "${selfcheck_report}")
message(STATUS "assembly-validation[selfcheck] ${selfcheck_report}")

# ---------------------------------------------------------------------------
# 1. 四个变体：configure -> build -> 运行 Smoke -> 采集证据
# ---------------------------------------------------------------------------
set(asm_variants shared static zero-shared zero-static)

foreach(variant IN LISTS asm_variants)
    set(variant_dir "${WORK_DIR}/${variant}")
    set(build_dir   "${variant_dir}/build")
    file(MAKE_DIRECTORY "${variant_dir}")

    if(variant MATCHES "^zero-")
        set(profile "zero")
    else()
        set(profile "default")
    endif()
    if(variant MATCHES "static$")
        set(monolithic "ON")
    else()
        set(monolithic "OFF")
    endif()

    # ---- 1.1 configure（profile 是唯一产品开关，不传任何额外 BOOL） ----
    set(configure_args
        -S "${ENGINE_SOURCE_DIR}"
        -B "${build_dir}"
        -G "${GENERATOR}"
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        -DID_BUILD_BENCHMARK=OFF
        "-DID_MONOLITHIC_BUILD=${monolithic}"
        "-DID_ASSEMBLY_PROFILE=${profile}")
    asm_run(configure_rc "${variant_dir}/configure.log" "${CMAKE_COMMAND}" ${configure_args})
    file(READ "${variant_dir}/configure.log" configure_text)

    # mode 与 selected target 都从生成证据里取：configure 日志里没有 mode 行，而且
    # CMake 正则的字符类不支持 \n 转义，跨行匹配会静默截断（实测坑，改用 file(STRINGS) 按行处理）
    set(mode "unknown")
    set(selected_targets "-")

    # ---- 1.2 零插件变体：先只构建校验目标与绑定目标，证明它们能独立编译 ----
    set(zero_targets_rc "-")
    if(variant MATCHES "^zero-")
        asm_run(zero_targets_rc "${variant_dir}/build-validate-assembly-only.log"
            "${CMAKE_COMMAND}" --build "${build_dir}" --clean-first --parallel
            --target IDAssemblyValidate IDAssembly)
    endif()

    # ---- 1.3 全量构建（先 clean：STATIC/SHARED 共用 <engine>/bin，K6 既有行为） ----
    set(build_rc "-")
    asm_run(build_rc "${variant_dir}/build.log" "${CMAKE_COMMAND}" --build "${build_dir}" --clean-first --parallel)
    file(READ "${variant_dir}/build.log" build_text)
    set(build_warnings 0)
    if("${build_text}" MATCHES "warning:")
        set(build_warnings 1)
    endif()

    # ---- 1.4 运行产品消费端（start / 宿主分配 / shutdown）并按第 0 节逐条点名判定 ----
    set(smoke_exe "${build_dir}/Assemble/out/bin/IDAssemblySmoke.exe")
    set(smoke_rc "missing-exe")
    set(smoke_pass 0)
    set(smoke_contract "not-run")
    set(smoke_problems "")
    if(EXISTS "${smoke_exe}")
        asm_run(smoke_rc "${variant_dir}/smoke.log" "${smoke_exe}")
        file(READ "${variant_dir}/smoke.log" smoke_text)
        # [PASS] 次数只作信息记录，不作为判定
        string(REGEX MATCHALL "\\[PASS\\]" smoke_pass_hits "${smoke_text}")
        list(LENGTH smoke_pass_hits smoke_pass)
        # 判定：实际 stdout 必须与 4 条断言逐条一致
        asm_smoke_output_problems(smoke_problems "${variant_dir}/smoke.log")
        file(WRITE "${variant_dir}/smoke-contract.txt" "${smoke_problems}")
        if(smoke_problems STREQUAL "")
            set(smoke_contract "name-by-name-ok")
        else()
            set(smoke_contract "failed")
        endif()
    endif()

    # ---- 1.5 采集：生成物 / 证据 / 身份宏 / 绑定 TU / 产物 / 链接行 / 符号 ----
    # 生成目录名来自 profile 的 assembly_name（切换过 profile 的构建目录会同时留着旧目录，
    # 所以按 profile 声明取本变体应得的那一个，而不是要求目录只有一个）
    set(profile_file "${ENGINE_SOURCE_DIR}/Assemble/profiles/${profile}.json")
    set(expected_assembly "")
    if(EXISTS "${profile_file}")
        file(READ "${profile_file}" profile_text)
        if("${profile_text}" MATCHES "\"assembly_name\"[ \t]*:[ \t]*\"([^\"]*)\"")
            set(expected_assembly "${CMAKE_MATCH_1}")
        endif()
    endif()
    if("${expected_assembly}" STREQUAL "")
        asm_add_problem("[${variant}] 无法从 ${profile_file} 读出 assembly_name")
    endif()

    set(gen_dirs "")
    if(EXISTS "${build_dir}/Assemble/generated")
        file(GLOB gen_candidates LIST_DIRECTORIES true "${build_dir}/Assemble/generated/*")
        foreach(candidate IN LISTS gen_candidates)
            if(IS_DIRECTORY "${candidate}")
                list(APPEND gen_dirs "${candidate}")
            endif()
        endforeach()
    endif()
    set(gen_dir "${build_dir}/Assemble/generated/${expected_assembly}")
    if(NOT IS_DIRECTORY "${gen_dir}")
        set(gen_dir "-")
        asm_add_problem("[${variant}] profile=${profile} 没有生成 ${expected_assembly} 目录（实际：${gen_dirs}）")
    endif()

    set(generated_files "-")
    set(evidence_text "")
    set(evidence_normalized "")
    set(binding_sha "-")
    set(export_headers "")
    set(binding_text "")
    if(NOT "${gen_dir}" STREQUAL "-")
        file(GLOB generated_files RELATIVE "${gen_dir}" "${gen_dir}/*")
        list(SORT generated_files)
        foreach(name IN LISTS generated_files)
            if(name MATCHES "Export\\.generated\\.hpp$")
                list(APPEND export_headers "${name}")
            endif()
        endforeach()
        if(EXISTS "${gen_dir}/IDAssemblyEvidence.txt")
            file(READ "${gen_dir}/IDAssemblyEvidence.txt" evidence_text)
            # mode 按构建模式不同是设计意图，比较前只把这一行归一化
            string(REGEX REPLACE "mode[ \t]*:[ \t]*(STATIC|SHARED)" "mode : MODE" evidence_normalized "${evidence_text}")
            # 构建模式只从证据文件判定：configure 日志里没有 mode 行
            if("${evidence_text}" MATCHES "mode[ \t]*:[ \t]*(STATIC|SHARED)")
                set(mode "${CMAKE_MATCH_1}")
            endif()
            # selected target 集合：逐行取 evidence 里的 "selected  <名字> target=<目标> ..."
            file(STRINGS "${gen_dir}/IDAssemblyEvidence.txt" selected_evid_lines REGEX "^selected ")
            set(selected_target_list "")
            foreach(evid_line IN LISTS selected_evid_lines)
                if(evid_line MATCHES "target=([^ ]+)")
                    list(APPEND selected_target_list "${CMAKE_MATCH_1}")
                endif()
            endforeach()
            if(selected_target_list)
                list(SORT selected_target_list)
                string(REPLACE ";" ";" selected_targets "${selected_target_list}")
            endif()
        endif()
        if(EXISTS "${gen_dir}/IDAssemblyBinding.generated.cpp")
            file(SHA256 "${gen_dir}/IDAssemblyBinding.generated.cpp" binding_sha)
            file(READ "${gen_dir}/IDAssemblyBinding.generated.cpp" binding_text)
            file(COPY "${gen_dir}/IDAssemblyBinding.generated.cpp" DESTINATION "${variant_dir}")
        endif()
    endif()

    # 身份宏：只取 ID_USE_PLUGIN_*（build 宏 ID_<TOKEN>_BUILD_DLL 属于私有实现细节，单独记录）
    set(macro_report "")
    set(identity_macro_lines "")
    set(identity_macros_seen "")
    file(GLOB_RECURSE flags_files "${build_dir}/Assemble/*flags.make")
    list(SORT flags_files)
    foreach(flags_file IN LISTS flags_files)
        file(READ "${flags_file}" flags_text)
        string(REGEX MATCHALL "ID_USE_PLUGIN_[A-Za-z0-9_]+" use_hits "${flags_text}")
        list(SORT use_hits)
        list(REMOVE_DUPLICATES use_hits)
        string(REPLACE "${build_dir}/Assemble/" "" flags_rel "${flags_file}")
        string(REGEX REPLACE "/CMakeFiles/.*$" "" flags_rel "${flags_rel}")
        string(REPLACE ";" " " use_text "${use_hits}")
        string(APPEND macro_report "${flags_rel} : ${use_text}\n")
        string(APPEND identity_macro_lines "${flags_rel} : ${use_text}\n")
        # 全量身份宏出现过的名字（用于 zero profile 的"未选中不出现"判定）
        foreach(hit IN LISTS use_hits)
            list(APPEND identity_macros_seen "${hit}")
        endforeach()
    endforeach()

    # 产物：out/lib 与 out/bin
    set(artifact_tokens "")
    set(artifact_files "")
    foreach(out_sub IN ITEMS lib bin)
        set(out_dir "${build_dir}/Assemble/out/${out_sub}")
        if(EXISTS "${out_dir}")
            file(GLOB out_files RELATIVE "${out_dir}" "${out_dir}/*")
            list(SORT out_files)
            foreach(out_file IN LISTS out_files)
                asm_artifact_token(token "${out_file}")
                list(APPEND artifact_tokens "${token}")
                list(APPEND artifact_files "${out_sub}/${out_file}")
            endforeach()
        endif()
    endforeach()
    set(artifact_stems "")
    foreach(token IN LISTS artifact_tokens)
        string(REGEX REPLACE ":[a-z]+$" "" stem "${token}")
        list(APPEND artifact_stems "${stem}")
    endforeach()
    if(artifact_stems)
        list(REMOVE_DUPLICATES artifact_stems)
        list(SORT artifact_stems)
    endif()
    string(REPLACE ";" " " artifact_token_text "${artifact_tokens}")
    string(REPLACE ";" " " artifact_stem_text "${artifact_stems}")
    # 内核制品（ID）由根 CMakeLists 强制输出到 <engine>/bin，两种模式的文件名形态不同（.a / .dll），
    # 而且不在组装层 out/ 里；跨模式比较的是"插件 + 组装 + 消费端"的模块集合，内核单独记录
    set(product_stems ${artifact_stems})
    list(REMOVE_ITEM product_stems "ID")
    string(REPLACE ";" " " product_stem_text "${product_stems}")

    # 链接行：whole-archive 只能包住桩（以及 CMake 自己给 EXE 对象归档加的那一对）
    # MinGW/Make 的实际链接命令在 link.txt + linkLibs.rsp 两个文件里，必须合并后扫描区间
    set(whole_archive_state "no-link-line")
    set(regions_report "")
    set(smoke_link_file "${build_dir}/Assemble/CMakeFiles/IDAssemblySmoke.dir/link.txt")
    if(EXISTS "${smoke_link_file}")
        file(COPY "${smoke_link_file}" DESTINATION "${variant_dir}")
        file(READ "${smoke_link_file}" link_text)
        set(link_combined "${link_text}")
        set(rsp_file "${build_dir}/Assemble/CMakeFiles/IDAssemblySmoke.dir/linkLibs.rsp")
        if(EXISTS "${rsp_file}")
            file(COPY "${rsp_file}" DESTINATION "${variant_dir}")
            file(READ "${rsp_file}" rsp_text)
            string(APPEND link_combined " ${rsp_text}")
        endif()
        file(WRITE "${variant_dir}/link-combined.txt" "${link_combined}")

        # 逐对扫描 --whole-archive ... --no-whole-archive 区间，记录每段包住了什么
        set(rest "${link_combined}")
        set(region_count 0)
        set(stub_region_count 0)
        set(plugin_region_count 0)
        set(objects_region_count 0)
        while(rest MATCHES "--whole-archive")
            string(FIND "${rest}" "--whole-archive" wa_pos)
            # 只在起点之后找结束标记：rest 可能正好以 --no-whole-archive 开头，
            # 若在整段里找会拿到"上一个区间"的结束标记，算出负长度
            string(SUBSTRING "${rest}" ${wa_pos} -1 after_wa)
            string(FIND "${after_wa}" "--no-whole-archive" nwa_rel)
            if(nwa_rel EQUAL -1)
                string(APPEND regions_report "unbalanced ")
                break()
            endif()
            math(EXPR nwa_pos "${wa_pos} + ${nwa_rel}")
            math(EXPR inner_len "${nwa_pos} - ${wa_pos}")
            string(SUBSTRING "${rest}" ${wa_pos} ${inner_len} region)
            math(EXPR region_count "${region_count} + 1")
            if(region MATCHES "IDPluginStub")
                math(EXPR stub_region_count "${stub_region_count} + 1")
                string(APPEND regions_report "region${region_count}=stub ")
            elseif(region MATCHES "objects\\.a")
                math(EXPR objects_region_count "${objects_region_count} + 1")
                string(APPEND regions_report "region${region_count}=exe-objects ")
            elseif(region MATCHES "IDFixture")
                math(EXPR plugin_region_count "${plugin_region_count} + 1")
                string(APPEND regions_report "region${region_count}=PLUGIN ")
            else()
                string(APPEND regions_report "region${region_count}=other ")
            endif()
            string(SUBSTRING "${rest}" ${nwa_pos} -1 rest)
        endwhile()

        set(whole_archive_state "regions=${region_count} stub=${stub_region_count} plugin=${plugin_region_count} ${regions_report}")
        if(stub_region_count LESS 1)
            asm_add_problem("[${variant}] --whole-archive 区间里没有 IDPluginStub（宿主分配桩没有被保留）")
        endif()
        if(plugin_region_count GREATER 0)
            asm_add_problem("[${variant}] --whole-archive 区间里出现了插件库（禁止全插件保留）")
        endif()
    else()
        asm_add_problem("[${variant}] 找不到 IDAssemblySmoke 的 link.txt")
    endif()

    # SHARED：每个模块的链接行里桩也要单独被 whole-archive 保留（每模块接入自己的桩）
    set(module_link_report "")
    if(mode STREQUAL "SHARED")
        file(GLOB_RECURSE plugin_link_files "${build_dir}/Assemble/plugins/*/CMakeFiles/*.dir/link.txt")
        foreach(plugin_link IN LISTS plugin_link_files)
            file(READ "${plugin_link}" plugin_link_text)
            set(plugin_rsp "${plugin_link}")
            string(REPLACE "/link.txt" "/linkLibs.rsp" plugin_rsp "${plugin_rsp}")
            if(EXISTS "${plugin_rsp}")
                file(READ "${plugin_rsp}" plugin_rsp_text)
                string(APPEND plugin_link_text " ${plugin_rsp_text}")
            endif()
            # 逐区间分类：允许"模块自己的 objects.a"与"IDPluginStub"两类，其余出现插件名即违规
            string(REGEX REPLACE ".*/plugins/([^/]+)/.*" "\\1" module_name "${plugin_link}")
            set(module_rest "${plugin_link_text}")
            set(module_stub_regions 0)
            set(module_plugin_regions 0)
            while(module_rest MATCHES "--whole-archive")
                string(FIND "${module_rest}" "--whole-archive" module_wa_pos)
                string(SUBSTRING "${module_rest}" ${module_wa_pos} -1 module_after_wa)
                string(FIND "${module_after_wa}" "--no-whole-archive" module_nwa_rel)
                if(module_nwa_rel EQUAL -1)
                    break()
                endif()
                math(EXPR module_nwa_pos "${module_wa_pos} + ${module_nwa_rel}")
                math(EXPR module_inner_len "${module_nwa_pos} - ${module_wa_pos}")
                string(SUBSTRING "${module_rest}" ${module_wa_pos} ${module_inner_len} module_region)
                if(module_region MATCHES "IDPluginStub")
                    math(EXPR module_stub_regions "${module_stub_regions} + 1")
                elseif(NOT module_region MATCHES "objects\\.a")
                    if(module_region MATCHES "IDFixture")
                        math(EXPR module_plugin_regions "${module_plugin_regions} + 1")
                    endif()
                endif()
                string(SUBSTRING "${module_rest}" ${module_nwa_pos} -1 module_rest)
            endwhile()

            set(module_state "stub=${module_stub_regions} plugin=${module_plugin_regions}")
            string(APPEND module_link_report "${module_name}: ${module_state}\n")
            if(module_stub_regions LESS 1)
                asm_add_problem("[${variant}] SHARED 模块 ${module_name} 没有用 whole-archive 接入 IDPluginStub（${module_state}）")
            endif()
            if(module_plugin_regions GREATER 0)
                asm_add_problem("[${variant}] SHARED 模块 ${module_name} 对插件库做了 whole-archive（${module_state}）")
            endif()
        endforeach()
    endif()

    # 替换符号：nm 逐个重载统计定义数
    set(symbol_state "no-nm")
    set(symbol_file "${variant_dir}/symbols-smoke.txt")
    if(NOT "${NM_TOOL}" STREQUAL "" AND EXISTS "${smoke_exe}")
        asm_run(nm_rc "${symbol_file}" "${NM_TOOL}" -C --defined-only "${smoke_exe}")
        if(nm_rc EQUAL 0)
            # 用 file(STRINGS) 按行切分：正则里不要用 [^\r\n]（CMake 会把 \r 当字面 r）
            file(STRINGS "${symbol_file}" nm_lines REGEX " T operator (new|delete)")
            list(LENGTH nm_lines replace_total)
            set(replace_signatures "")
            foreach(nm_line IN LISTS nm_lines)
                string(REGEX REPLACE ".* T (operator .*)" "\\1" nm_signature "${nm_line}")
                list(APPEND replace_signatures "${nm_signature}")
            endforeach()
            set(replace_unique ${replace_signatures})
            if(replace_unique)
                list(REMOVE_DUPLICATES replace_unique)
            endif()
            list(LENGTH replace_unique replace_dup_free)
            set(symbol_state "total=${replace_total} unique=${replace_dup_free}")
            if(NOT replace_total EQUAL replace_dup_free)
                asm_add_problem("[${variant}] 最终程序里出现重复的 new/delete 替换符号定义（total=${replace_total} unique=${replace_dup_free}）")
            endif()
            if(replace_total LESS 16)
                asm_add_problem("[${variant}] 最终程序的 new/delete 替换符号少于桩声明的 16 个重载（total=${replace_total}）")
            endif()
            # 逐个签名必须恰好一次（上面已判重），且两个最基本的替换符号必须在场
            list(FIND replace_unique "operator new(unsigned long long)" new_basic)
            list(FIND replace_unique "operator delete(void*)" delete_basic)
            if(new_basic EQUAL -1)
                asm_add_problem("[${variant}] 最终程序缺少 operator new(unsigned long long) 定义")
            endif()
            if(delete_basic EQUAL -1)
                asm_add_problem("[${variant}] 最终程序缺少 operator delete(void*) 定义")
            endif()
        else()
            set(symbol_state "nm-failed rc=${nm_rc}")
            asm_add_problem("[${variant}] nm 解析 ${smoke_exe} 失败（rc=${nm_rc}）")
        endif()
    elseif("${NM_TOOL}" STREQUAL "")
        if("${CXX_COMPILER_ID}" MATCHES "GNU|Clang")
            asm_add_problem("[${variant}] 未提供 nm：替换符号唯一性未验证")
        else()
            asm_add_note("[${variant}] 未提供 nm（${CXX_COMPILER_ID}）：替换符号唯一性未验证")
        endif()
    endif()

    # 共享模式下每个插件 DLL 各自带桩：逐个 DLL 统计 new/delete 定义
    set(module_stub_report "")
    if(mode STREQUAL "SHARED" AND NOT "${NM_TOOL}" STREQUAL "")
        foreach(stem IN LISTS artifact_stems)
            if(stem STREQUAL "ID" OR stem STREQUAL "IDAssembly" OR stem STREQUAL "IDPluginStub" OR stem STREQUAL "IDAssemblySmoke")
                continue()
            endif()
            set(dll_path "${build_dir}/Assemble/out/bin/lib${stem}.dll")
            if(EXISTS "${dll_path}")
                set(dll_symbol_file "${variant_dir}/symbols-${stem}.txt")
                asm_run(dll_nm_rc "${dll_symbol_file}" "${NM_TOOL}" -C --defined-only "${dll_path}")
                file(STRINGS "${dll_symbol_file}" dll_lines REGEX " T operator (new|delete)")
                list(LENGTH dll_lines dll_replace_count)
                string(APPEND module_stub_report "${stem}: replace_defs=${dll_replace_count}\n")
                if(dll_replace_count LESS 16)
                    asm_add_problem("[${variant}] SHARED 模块 ${stem} 的 new/delete 桩不完整（${dll_replace_count} 个定义）")
                endif()
            endif()
        endforeach()
    endif()

    # ---- 1.6 逐变体汇总 ----
    # 用 string(CONCAT) 而不是多参数 set()：后者生成 CMake 列表，写盘时每行之间会多出 ';'
    string(CONCAT variant_summary
        "VARIANT          : ${variant}\n"
        "PROFILE          : ${profile}\n"
        "ID_MONOLITHIC    : ${monolithic}\n"
        "MODE             : ${mode}\n"
        "CONFIGURE_EXIT   : ${configure_rc}\n"
        "ZERO_TARGETS_EXIT: ${zero_targets_rc}\n"
        "BUILD_EXIT       : ${build_rc}\n"
        "BUILD_WARNINGS   : ${build_warnings}\n"
        "SMOKE_EXIT       : ${smoke_rc}\n"
        "SMOKE_PASS_LINES : ${smoke_pass}\n"
        "SMOKE_CONTRACT   : ${smoke_contract}\n"
        "SELECTED_TARGETS : ${selected_targets}\n"
        "GENERATED_DIR    : ${gen_dir}\n"
        "GENERATED_FILES  : ${generated_files}\n"
        "EXPORT_HEADERS   : ${export_headers}\n"
        "BINDING_SHA256   : ${binding_sha}\n"
        "ARTIFACTS        : ${artifact_token_text}\n"
        "ARTIFACT_STEMS   : ${artifact_stem_text}\n"
        "PRODUCT_STEMS    : ${product_stem_text}\n"
        "MODULE_LINKS     : ${module_link_report}\n"
        "WHOLE_ARCHIVE    : ${whole_archive_state}\n"
        "REPLACE_SYMBOLS  : ${symbol_state}\n"
        "MODULE_STUBS     : ${module_stub_report}")
    file(WRITE "${variant_dir}/variant-summary.txt" "${variant_summary}")
    file(WRITE "${variant_dir}/identity-macros.txt" "${macro_report}")

    if(NOT configure_rc EQUAL 0)
        asm_add_problem("[${variant}] configure exit=${configure_rc}（见 ${variant_dir}/configure.log）")
    endif()
    if(NOT zero_targets_rc STREQUAL "-" AND NOT zero_targets_rc EQUAL 0)
        asm_add_problem("[${variant}] IDAssemblyValidate/IDAssembly 不能单独构建（exit=${zero_targets_rc}）")
    endif()
    if(NOT build_rc EQUAL 0)
        asm_add_problem("[${variant}] build exit=${build_rc}（见 ${variant_dir}/build.log）")
    elseif(build_warnings)
        asm_add_problem("[${variant}] build 出现 warning 行")
    endif()
    if(NOT smoke_rc EQUAL 0)
        asm_add_problem("[${variant}] IDAssemblySmoke exit=${smoke_rc}（见 ${variant_dir}/smoke.log）")
    endif()
    if(smoke_contract STREQUAL "failed")
        # 逐条点名失败项都进批次问题，不合并成一条"数量不符"
        string(REPLACE "\n" ";" smoke_problem_lines "${smoke_problems}")
        foreach(problem_line IN LISTS smoke_problem_lines)
            if(NOT problem_line STREQUAL "")
                asm_add_problem("[${variant}] ${problem_line}")
            endif()
        endforeach()
    elseif(smoke_contract STREQUAL "not-run")
        asm_add_problem("[${variant}] 找不到 ${smoke_exe}：Smoke 点名断言没有执行")
    endif()

    # 记录本次变体的关键值，供后面的跨模式比较使用
    set(${variant}_mode "${mode}")
    set(${variant}_selected "${selected_targets}")
    set(${variant}_evidence "${evidence_normalized}")
    set(${variant}_binding_sha "${binding_sha}")
    set(${variant}_stems "${artifact_stem_text}")
    set(${variant}_product_stems "${product_stem_text}")
    set(${variant}_tokens "${artifact_token_text}")
    set(${variant}_macros "${identity_macro_lines}")
    set(${variant}_gen_dir "${gen_dir}")
    set(${variant}_exports "${export_headers}")
    set(${variant}_binding_text_sha "${binding_sha}")
    set(${variant}_build_dir "${build_dir}")
    set(${variant}_smoke_rc "${smoke_rc}")
    set(${variant}_undefined "${identity_macros_seen}")

    message(STATUS "assembly-validation[${variant}] mode=${mode} configure=${configure_rc} build=${build_rc} smoke=${smoke_rc} pass=${smoke_pass}")
endforeach()

# ---------------------------------------------------------------------------
# 2. STATIC 与 SHARED（default profile）一致性
# ---------------------------------------------------------------------------
set(evidence_equal FALSE)
if("${static_evidence}" STREQUAL "${shared_evidence}")
    set(evidence_equal TRUE)
endif()

if(NOT "${static_selected}" STREQUAL "${shared_selected}")
    asm_add_problem("STATIC/SHARED 的 selected target 集合不一致：\n  STATIC：${static_selected}\n  SHARED：${shared_selected}")
endif()
if(NOT "${static_macros}" STREQUAL "${shared_macros}")
    asm_add_problem("STATIC/SHARED 的逐插件身份宏不一致：\n  STATIC：\n${static_macros}\n  SHARED：\n${shared_macros}")
endif()
if(NOT "${static_evidence}" STREQUAL "${shared_evidence}")
    asm_add_problem("STATIC/SHARED 的构建证据（provider/边/槽/目标/身份）不一致：\n  STATIC：\n${static_evidence}\n  SHARED：\n${shared_evidence}")
endif()
if(NOT "${static_binding_sha}" STREQUAL "${shared_binding_sha}")
    asm_add_problem("STATIC/SHARED 的运行期绑定 TU 内容不一致：${static_binding_sha} vs ${shared_binding_sha}")
endif()
if(NOT "${static_product_stems}" STREQUAL "${shared_product_stems}")
    asm_add_problem("STATIC/SHARED 的产物模块集合不一致（不含内核 ID 制品）：\n  STATIC：${static_product_stems}\n  SHARED：${shared_product_stems}")
endif()

# ---------------------------------------------------------------------------
# 3. 未选中插件不得出现在宏 / 链接边 / 绑定 TU / 产物 / 导出头（zero profile = 全部未选中）
# ---------------------------------------------------------------------------
foreach(variant IN ITEMS zero-shared zero-static)
    if(NOT "${${variant}_undefined}" STREQUAL "")
        asm_add_problem("[${variant}] zero profile 下仍出现身份宏：${${variant}_undefined}")
    endif()
    if("${${variant}_stems}" MATCHES "IDFixture")
        asm_add_problem("[${variant}] zero profile 下产物里出现插件模块：${${variant}_stems}")
    endif()
    if("${${variant}_exports}" MATCHES "IDFixture")
        asm_add_problem("[${variant}] zero profile 下仍生成插件导出头：${${variant}_exports}")
    endif()
    set(binding_copy "${WORK_DIR}/${variant}/IDAssemblyBinding.generated.cpp")
    if(EXISTS "${binding_copy}")
        file(READ "${binding_copy}" zero_binding_text)
        if(zero_binding_text MATCHES "IDFixture")
            asm_add_problem("[${variant}] zero profile 的绑定 TU 里出现了未选中插件名：${binding_copy}")
        endif()
        if(NOT zero_binding_text MATCHES "\\.records         = kSelectedCount > 0 \\? kRecords.data\\(\\) : nullptr")
            asm_add_problem("[${variant}] zero profile 的绑定 TU 没有给出零插件空视图（records 应为 nullptr）")
        endif()
    endif()
    # 证据目录必须存在（1.5 已按 profile 的 assembly_name 定位，这里只复核一次）
    if(NOT "${${variant}_gen_dir}" STREQUAL "-" AND NOT IS_DIRECTORY "${${variant}_gen_dir}")
        asm_add_problem("[${variant}] zero profile 的证据目录不存在：${${variant}_gen_dir}")
    endif()
endforeach()

# 反过来：default profile 里也不得出现"未选中插件"的痕迹（本 catalog 三者全选中，
# 因此只验证不曾被选中的占位宏没有泄漏进任何编译单元）
if("${shared_undefined}" MATCHES "ID_USE_PLUGIN_IDFIXTURENOTSELECTED")
    asm_add_problem("[shared] 未选中插件的身份宏泄漏到编译单元")
endif()

# ---------------------------------------------------------------------------
# 4. 改坏构建证据：C++ 静态校验必须按具体错误类别拒绝
# ---------------------------------------------------------------------------
set(pristine_gen "${shared_gen_dir}")
set(corrupt_root "${WORK_DIR}/corrupt")
file(MAKE_DIRECTORY "${corrupt_root}")

set(corrupt_cases
    "provider:EvidenceProviderMismatch:Evidence"
    "edge:EvidenceEdgeMismatch:Evidence"
    "identity:EvidenceIdentityMismatch:Evidence"
    "slot:EvidenceSlotMismatch:Evidence"
    "selection:EvidenceSelectionMismatch:Evidence"
    "target:EvidenceTargetMismatch:Evidence"
    "macro:EvidenceMacroMismatch:Evidence"
    "runtime:RuntimeBindingMismatch:Evidence")

# 编译一个源文件（只做语法/语义检查，不链接）：asm_compile(<rc> <log> <源文件> <包含目录...>)
function(asm_compile out_rc log_path source_file)
    set(include_args "")
    foreach(dir IN LISTS ARGN)
        list(APPEND include_args "-I${dir}")
    endforeach()
    execute_process(
        COMMAND "${CXX_COMPILER}" -std=c++20 -fsyntax-only -fno-rtti -fno-exceptions
                -DID_STATIC_PLUGIN_LINK=1 ${include_args} "${source_file}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE compile_rc
        OUTPUT_FILE "${log_path}"
        ERROR_FILE "${log_path}.err")
    set(${out_rc} "${compile_rc}" PARENT_SCOPE)
endfunction()

# asm_extract_line(<out_line> <text> <token>)：取出包含 token 的整行（不含行尾）
# 不用正则跨行匹配：CMake 正则把 \r 当字面 r，容易静默匹配失败
function(asm_extract_line out_line text token)
    set(${out_line} "" PARENT_SCOPE)
    string(FIND "${text}" "${token}" token_pos)
    if(token_pos EQUAL -1)
        return()
    endif()
    string(SUBSTRING "${text}" ${token_pos} -1 tail)
    string(FIND "${tail}" "\n" line_end)
    if(line_end EQUAL -1)
        set(line "${tail}")
    else()
        string(SUBSTRING "${tail}" 0 ${line_end} line)
    endif()
    set(${out_line} "${line}" PARENT_SCOPE)
endfunction()

# 把"某一类证据改坏"的生成头写进独立目录：先取出证据所在整行，再对行内做**字面**替换，
# 最后用整行替换回全文——每类只改一处，且必须真的改动成功（否则视为驱动器失败，不静默放过）
function(asm_make_corrupt_case case_name pristine_dir target_dir)
    file(COPY "${pristine_dir}/IDAssembly.generated.hpp" "${pristine_dir}/IDAssemblyValidate.generated.cpp"
         "${PROBE_SOURCE}" DESTINATION "${target_dir}")
    file(READ "${target_dir}/IDAssembly.generated.hpp" header_text)

    set(line_token "")
    set(old_snippet "")
    set(new_snippet "")
    if(case_name STREQUAL "provider")
        set(line_token "evidence.bindings[0]")
        set(old_snippet ".provider = \"ID.Fixture.Core\"")
        set(new_snippet ".provider = \"ID.Fixture.Api\"")
    elseif(case_name STREQUAL "edge")
        set(line_token "evidence.edges[0]")
        set(old_snippet ".from = \"ID.Fixture.Core\", .to = \"ID.Fixture.Math\"")
        set(new_snippet ".from = \"ID.Fixture.Math\", .to = \"ID.Fixture.Core\"")
    elseif(case_name STREQUAL "identity")
        set(line_token "evidence.selected[0]")
        set(old_snippet ".plugin_id = 0x417557752be36759ull")
        set(new_snippet ".plugin_id = 0x417557752be3675aull")
    elseif(case_name STREQUAL "slot")
        set(line_token "evidence.slots[0]")
        set(old_snippet ".slot = 60")
        set(new_snippet ".slot = 61")
    elseif(case_name STREQUAL "selection")
        set(line_token "evidence.selected[0]")
        set(old_snippet ".name = \"ID.Fixture.Api\"")
        set(new_snippet ".name = \"ID.Fixture.NotSelected\"")
    elseif(case_name STREQUAL "target")
        set(line_token "evidence.selected[0]")
        set(old_snippet ".target = \"IDFixtureApi\"")
        set(new_snippet ".target = \"IDFixtureWrongTarget\"")
    elseif(case_name STREQUAL "macro")
        set(line_token "evidence.selected[0]")
        set(old_snippet ".macro = \"ID_USE_PLUGIN_IDFIXTUREAPI\"")
        set(new_snippet ".macro = \"ID_USE_PLUGIN_WRONGMACRO\"")
    elseif(case_name STREQUAL "runtime")
        set(line_token "evidence.runtime[0]")
        set(old_snippet ".api_revision = 1")
        set(new_snippet ".api_revision = 9")
    else()
        message(FATAL_ERROR "未知的证据改坏类别：${case_name}")
    endif()

    asm_extract_line(original_line "${header_text}" "${line_token}")
    if(original_line STREQUAL "")
        message(FATAL_ERROR "改坏证据用例 ${case_name}：生成头里找不到证据行 ${line_token}")
    endif()
    string(FIND "${original_line}" "${old_snippet}" snippet_pos)
    if(snippet_pos EQUAL -1)
        message(FATAL_ERROR
            "改坏证据用例 ${case_name}：证据行里没有期望的原始片段 ${old_snippet}\n  实际行：${original_line}")
    endif()
    string(REPLACE "${old_snippet}" "${new_snippet}" mutated_line "${original_line}")
    string(REPLACE "${original_line}" "${mutated_line}" mutated "${header_text}")
    if(mutated STREQUAL header_text)
        message(FATAL_ERROR "改坏证据用例 ${case_name}：替换没有改变生成头内容")
    endif()
    file(WRITE "${target_dir}/IDAssembly.generated.hpp" "${mutated}")
endfunction()

set(legal_dir "${corrupt_root}/legal")
file(MAKE_DIRECTORY "${legal_dir}")
file(COPY "${pristine_gen}/IDAssembly.generated.hpp" "${pristine_gen}/IDAssemblyValidate.generated.cpp" "${PROBE_SOURCE}"
     DESTINATION "${legal_dir}")

set(corrupt_report "")
set(index_lines "")

# asm_compile_expected(<rc> <log> <源文件> <期望错误类别> <期望阶段> <包含目录...>)
# 探针编译必须带上期望宏：类别/阶段与实现不符就是编译失败
function(asm_compile_expected out_rc log_path source_file expect_code expect_phase)
    set(include_args "")
    foreach(dir IN LISTS ARGN)
        list(APPEND include_args "-I${dir}")
    endforeach()
    execute_process(
        COMMAND "${CXX_COMPILER}" -std=c++20 -fsyntax-only -fno-rtti -fno-exceptions -DID_STATIC_PLUGIN_LINK=1
                "-DPROBE_EXPECT_CODE=${expect_code}" "-DPROBE_EXPECT_PHASE=${expect_phase}"
                ${include_args} "${source_file}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE compile_rc
        OUTPUT_FILE "${log_path}"
        ERROR_FILE "${log_path}.err")
    set(${out_rc} "${compile_rc}" PARENT_SCOPE)
endfunction()

asm_compile_expected(legal_probe_rc "${corrupt_root}/legal-probe.log"
    "${legal_dir}/EvidenceCategoryProbe.cpp" "None" "None" "${legal_dir}" "${ENGINE_SOURCE_DIR}/ID/include")
asm_compile(legal_validate_rc "${corrupt_root}/legal-validate.log"
    "${legal_dir}/IDAssemblyValidate.generated.cpp" "${legal_dir}" "${ENGINE_SOURCE_DIR}/ID/include")

set(legal_state "probe=${legal_probe_rc} validate=${legal_validate_rc}")
if(NOT legal_probe_rc EQUAL 0)
    asm_add_problem("合法对照探针编译失败（expect=None/None，exit=${legal_probe_rc}）：环境或生成输入有问题，后续反向用例不可信")
endif()
if(NOT legal_validate_rc EQUAL 0)
    asm_add_problem("合法对照的真实校验 TU 编译失败（exit=${legal_validate_rc}）：环境或生成输入有问题，后续反向用例不可信")
endif()

foreach(entry IN LISTS corrupt_cases)
    string(REPLACE ":" ";" case_fields "${entry}")
    list(GET case_fields 0 case_name)
    list(GET case_fields 1 expect_code)
    list(GET case_fields 2 expect_phase)

    set(case_dir "${corrupt_root}/${case_name}")
    file(MAKE_DIRECTORY "${case_dir}")
    asm_make_corrupt_case("${case_name}" "${pristine_gen}" "${case_dir}")

    # 4.2 类别断言探针：期望类别必须与实现返回的类别相等，否则编译失败
    asm_compile_expected(case_probe_rc "${corrupt_root}/${case_name}-probe.log"
        "${case_dir}/EvidenceCategoryProbe.cpp" "${expect_code}" "${expect_phase}"
        "${case_dir}" "${ENGINE_SOURCE_DIR}/ID/include")

    # 4.3 真实校验 TU：改坏证据后必须编译失败（产品构建的静态门控）
    asm_compile(case_validate_rc "${corrupt_root}/${case_name}-validate.log"
        "${case_dir}/IDAssemblyValidate.generated.cpp" "${case_dir}" "${ENGINE_SOURCE_DIR}/ID/include")

    # 4.4 反向探针：把期望写成 None 必须编译失败，证明 4.2 的断言不是空转
    asm_compile_expected(case_none_rc "${corrupt_root}/${case_name}-probe-none.log"
        "${case_dir}/EvidenceCategoryProbe.cpp" "None" "None"
        "${case_dir}" "${ENGINE_SOURCE_DIR}/ID/include")

    string(APPEND corrupt_report
        "case ${case_name}: expect=${expect_code}/${expect_phase} probe_exit=${case_probe_rc} "
        "real_validate_exit=${case_validate_rc} wrong_expect_exit=${case_none_rc}\n")

    if(NOT case_probe_rc EQUAL 0)
        asm_add_problem("改坏 ${case_name} 证据后，C++ 校验没有返回期望的错误类别 ${expect_code}/${expect_phase}（探针 exit=${case_probe_rc}）")
    endif()
    if(case_validate_rc EQUAL 0)
        asm_add_problem("改坏 ${case_name} 证据后，真实校验 TU 仍然编译通过：产品构建的静态门控失效")
    endif()
    if(case_none_rc EQUAL 0)
        asm_add_problem("改坏 ${case_name} 证据后，期望 None 的反向探针也编译通过：类别断言是空转")
    endif()

    string(APPEND index_lines "CORRUPT ${case_name} expect=${expect_code}/${expect_phase} probe=${case_probe_rc} validate=${case_validate_rc} wrong_expect=${case_none_rc}\n")
endforeach()
file(WRITE "${WORK_DIR}/corrupt-evidence-summary.txt" "${corrupt_report}")
# 如实记录口径差异：RuntimeBindingMismatch 由证据阶段上报（phase=Evidence），
# AssemblyPhase::RuntimeMetadata 在当前实现里没有任何诊断会产生
asm_add_note("RuntimeBindingMismatch 的失败阶段是 Evidence（AssemblyPhase::RuntimeMetadata 目前无诊断使用）；反向用例按实际阶段断言")

# ---------------------------------------------------------------------------
# 5. 切换 profile：不删缓存也能重新配置并更新生成物
# ---------------------------------------------------------------------------
set(switch_report "")
set(switch_build "${shared_build_dir}")
set(switch_gen_before "${shared_gen_dir}/IDAssembly.generated.hpp")
set(switch_hash_before "-")
set(switch_time_before "-")
if(EXISTS "${switch_gen_before}")
    file(SHA256 "${switch_gen_before}" switch_hash_before)
    file(TIMESTAMP "${switch_gen_before}" switch_time_before "%Y-%m-%d %H:%M:%S")
endif()

# 5.1 先证明"无改动时重复构建不扰动生成物"（configure_file 内容相同不刷新时间戳）
asm_run(noop_build_rc "${WORK_DIR}/switch-noop-build.log"
    "${CMAKE_COMMAND}" --build "${switch_build}" --parallel --target IDAssemblyValidate)
set(switch_hash_noop "-")
set(switch_time_noop "-")
if(EXISTS "${switch_gen_before}")
    file(SHA256 "${switch_gen_before}" switch_hash_noop)
    file(TIMESTAMP "${switch_gen_before}" switch_time_noop "%Y-%m-%d %H:%M:%S")
endif()
if(NOT switch_hash_noop STREQUAL switch_hash_before)
    asm_add_problem("无改动重建后生成头内容变化：${switch_hash_before} -> ${switch_hash_noop}")
endif()
string(APPEND switch_report
    "noop: build_exit=${noop_build_rc} hash_before=${switch_hash_before} hash_after=${switch_hash_noop} "
    "time_before=${switch_time_before} time_after=${switch_time_noop}\n")

# 5.2 切到 zero（只改 cache 变量，不删任何缓存/目录）
asm_run(switch_zero_configure_rc "${WORK_DIR}/switch-zero-configure.log"
    "${CMAKE_COMMAND}" -S "${ENGINE_SOURCE_DIR}" -B "${switch_build}" "-DID_ASSEMBLY_PROFILE=zero")
asm_run(switch_zero_build_rc "${WORK_DIR}/switch-zero-build.log"
    "${CMAKE_COMMAND}" --build "${switch_build}" --parallel --target IDAssembly IDAssemblySmoke)
file(GLOB switch_zero_gens LIST_DIRECTORIES true "${switch_build}/Assemble/generated/*")
set(switch_zero_smoke_rc "missing-exe")
if(EXISTS "${switch_build}/Assemble/out/bin/IDAssemblySmoke.exe")
    asm_run(switch_zero_smoke_rc "${WORK_DIR}/switch-zero-smoke.log" "${switch_build}/Assemble/out/bin/IDAssemblySmoke.exe")
endif()
set(switch_zero_evidence "-")
if(EXISTS "${switch_build}/Assemble/generated/zero-fixture/IDAssemblyEvidence.txt")
    file(STRINGS "${switch_build}/Assemble/generated/zero-fixture/IDAssemblyEvidence.txt" zero_lines REGEX "^selected")
    if(zero_lines)
        list(GET zero_lines 0 switch_zero_evidence)
    endif()
endif()
string(APPEND switch_report
    "to_zero: configure_exit=${switch_zero_configure_rc} build_exit=${switch_zero_build_rc} smoke_exit=${switch_zero_smoke_rc} "
    "generated_dirs=${switch_zero_gens} evidence=${switch_zero_evidence}\n")

if(NOT switch_zero_configure_rc EQUAL 0)
    asm_add_problem("不删缓存切到 zero profile 的 configure 失败（exit=${switch_zero_configure_rc}）")
endif()
if(NOT switch_zero_build_rc EQUAL 0)
    asm_add_problem("不删缓存切到 zero profile 后构建失败（exit=${switch_zero_build_rc}）")
endif()
if(NOT switch_zero_smoke_rc EQUAL 0)
    asm_add_problem("切到 zero profile 后 Smoke exit=${switch_zero_smoke_rc}：生成物没有随 profile 更新")
endif()

# 5.3 再切回 default：生成物内容必须回到切换前的哈希
asm_run(switch_back_configure_rc "${WORK_DIR}/switch-default-configure.log"
    "${CMAKE_COMMAND}" -S "${ENGINE_SOURCE_DIR}" -B "${switch_build}" "-DID_ASSEMBLY_PROFILE=default")
asm_run(switch_back_build_rc "${WORK_DIR}/switch-default-build.log"
    "${CMAKE_COMMAND}" --build "${switch_build}" --parallel --target IDAssembly IDAssemblySmoke)
set(switch_back_smoke_rc "missing-exe")
if(EXISTS "${switch_build}/Assemble/out/bin/IDAssemblySmoke.exe")
    asm_run(switch_back_smoke_rc "${WORK_DIR}/switch-default-smoke.log" "${switch_build}/Assemble/out/bin/IDAssemblySmoke.exe")
endif()
set(switch_hash_back "-")
set(switch_time_back "-")
if(EXISTS "${switch_gen_before}")
    file(SHA256 "${switch_gen_before}" switch_hash_back)
    file(TIMESTAMP "${switch_gen_before}" switch_time_back "%Y-%m-%d %H:%M:%S")
endif()
string(APPEND switch_report
    "back_to_default: configure_exit=${switch_back_configure_rc} build_exit=${switch_back_build_rc} smoke_exit=${switch_back_smoke_rc} "
    "hash_after=${switch_hash_back} time_after=${switch_time_back}\n")
if(NOT switch_back_configure_rc EQUAL 0 OR NOT switch_back_build_rc EQUAL 0 OR NOT switch_back_smoke_rc EQUAL 0)
    asm_add_problem("切回 default profile 失败：configure=${switch_back_configure_rc} build=${switch_back_build_rc} smoke=${switch_back_smoke_rc}")
endif()
if(NOT switch_hash_back STREQUAL switch_hash_before)
    asm_add_problem("切回 default profile 后生成头内容与首次不同：${switch_hash_before} -> ${switch_hash_back}（生成不确定）")
endif()
file(WRITE "${WORK_DIR}/profile-switch-summary.txt" "${switch_report}")

# ---------------------------------------------------------------------------
# 6. 索引与结论
# ---------------------------------------------------------------------------
string(TIMESTAMP batch_finish "%Y-%m-%d %H:%M:%S")

file(WRITE "${RAW_LOG_DIR}/assembly-index.txt"
    "# P2 组装层校验批次索引（RunAssemblyValidation.cmake 自动写入）\n"
    "# 判定：判定器自检按预期拒绝错误 stdout；四个变体 configure/build/Smoke 全 0 且 Smoke 输出与\n"
    "#       4 条断言逐条一致（点名断言）；STATIC 与 SHARED 证据一致；\n"
    "#       zero profile 无未选中插件痕迹；8 类改坏证据全部按类别拒绝且有合法对照；\n"
    "#       profile 切换不删缓存即更新生成物；whole-archive 只桩 IDPluginStub；替换符号唯一\n"
    "MODE            : ${RAW_LOG_MODE}\n"
    "WORK_DIR        : ${WORK_DIR}\n"
    "GENERATOR       : ${GENERATOR}\n"
    "BUILD_TYPE      : ${BUILD_TYPE}\n"
    "CXX             : ${CXX_COMPILER}\n"
    "CXX_ID          : ${CXX_COMPILER_ID}\n"
    "STARTED         : ${batch_start}\n"
    "FINISHED        : ${batch_finish}\n"
    "SHARED_MODE     : ${shared_mode}\n"
    "STATIC_MODE     : ${static_mode}\n"
    "SHARED_SELECTED : ${shared_selected}\n"
    "STATIC_SELECTED : ${static_selected}\n"
    "SHARED_STEMS    : ${shared_stems}\n"
    "STATIC_STEMS    : ${static_stems}\n"
    "SHARED_BINDING  : ${shared_binding_sha}\n"
    "STATIC_BINDING  : ${static_binding_sha}\n"
    "EVIDENCE_EQUAL  : ${evidence_equal}\n"
    "LEGAL_CONTROL   : ${legal_state}\n"
    "SMOKE_SELFCHECK : ${selfcheck_report}"
    "${index_lines}"
    "PROFILE_SWITCH  : ${switch_report}"
    "NOTES           : ${notes}"
    "PROBLEMS        : ${problems}\n")

message(STATUS "assembly 校验：legal=${legal_state}")
message(STATUS "assembly 校验：STATIC=${static_mode}/${static_selected}  SHARED=${shared_mode}/${shared_selected}")
message(STATUS "assembly 校验：STATIC stems=${static_stems}")
message(STATUS "assembly 校验：SHARED stems=${shared_stems}")

if(problems)
    message(STATUS "assembly 校验问题：\n${problems}")
    message(FATAL_ERROR "assembly 校验批次未通过（详见 ${RAW_LOG_DIR}）")
endif()
message(STATUS
    "assembly 校验批次通过：判定器自检按预期拒绝错误输出；四个变体 configure/build/Smoke 全 0，"
    "Smoke 输出与 4 条断言逐条一致；STATIC 与 SHARED 证据一致；"
    "zero profile 无未选中痕迹；8 类改坏证据全部按类别拒绝；profile 切换与替换符号检查通过")
