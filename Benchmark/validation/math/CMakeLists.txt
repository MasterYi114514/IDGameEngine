cmake_minimum_required(VERSION 3.20)

# =============================================================================
# 数学域编译失败负例（默认 OFF 的校验资产，不是官方测试域）
#   - 不写官方 report.txt、不进官方 exe、不参与六档评级；
#   - 自带独立驱动（run_negative.cmake），用本工程同一个编译器 + 同一套 include/身份宏编译；
#   - 复用选中的 backend target 的 INTERFACE usage requirement（include 目录 + 身份宏），
#     不复制宏值、不在这里重写宏，也不新增 backend 专属开关；
#   - 判据：先编合法对照（必须通过），再逐个编"期望失败"用例，要求恰好 1 条静态断言 +
#     1 个 EXPECTED:<类别> + 0 个 MISMATCH（口径见 benchmark skill 第 6 节第 12 条）。
# =============================================================================

if(NOT ID_BUILD_VALIDATION_MATH)
    return()
endif()

set(MATH_VALIDATION_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MATH_VALIDATION_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${MATH_VALIDATION_OUT})

# 选中的数学 backend：只按"目标是否存在"判定，不写 profile 名字特判
set(MATH_VALIDATION_BACKENDS "")
foreach(candidate IN ITEMS IDMathScalar IDMathSIMD)
    if(TARGET ${candidate})
        list(APPEND MATH_VALIDATION_BACKENDS ${candidate})
    endif()
endforeach()

if(NOT MATH_VALIDATION_BACKENDS)
    message(FATAL_ERROR
        "ID_BUILD_VALIDATION_MATH=ON 但当前 profile 没有选中任何数学 backend（IDMathScalar / IDMathSIMD）。"
        "请用 -DID_ASSEMBLY_PROFILE=math-scalar|math-simd|math-both 配置。")
endif()

# 逐 backend 读出 PUBLIC usage requirement。这里只做一次"取用"，不改写：
# 身份宏与 include 目录的真源仍是插件自己的 target（组装层派生）。
# 递归收集一个 target 的 PUBLIC usage requirement（include 目录 + 编译定义），供负例 TU 使用。
# 只做"取用"：插件宏与 include 目录的真源仍是插件 target 自己，本驱动不复制宏值。
function(math_validation_collect_flags BACKEND OUT_FLAGS)
    get_target_property(backend_includes ${BACKEND} INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(backend_definitions ${BACKEND} INTERFACE_COMPILE_DEFINITIONS)
    if(NOT backend_includes)
        set(backend_includes "")
    endif()
    if(NOT backend_definitions)
        set(backend_definitions "")
    endif()

    # 累加语义：调用方可以为多个 target 连续调用本函数（插件 target + 它依赖的 ID / IDPluginStub）
    set(flags "")
    if(DEFINED ${OUT_FLAGS})
        set(flags ${${OUT_FLAGS}})
    endif()
    foreach(item IN LISTS backend_includes)
        # 逐项解析：BUILD_INTERFACE 取内层路径；INSTALL_INTERFACE 只对安装树有意义，本驱动跳过；
        # 其它未解析的生成器表达式直接拒绝（不静默跳过，否则会编到错误的头）
        string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" resolved "${item}")
        if(resolved MATCHES "^\\$<INSTALL_INTERFACE:")
            continue()
        endif()
        if(resolved MATCHES "^\\$<")
            message(FATAL_ERROR
                "${BACKEND} 的 INTERFACE_INCLUDE_DIRECTORIES 含无法解析的生成器表达式：${item}"
                "（负例驱动只会用可直接传入编译器的构建树路径）")
        endif()
        if(MSVC)
            list(APPEND flags "/I${resolved}")
        else()
            list(APPEND flags "-I${resolved}")
        endif()
    endforeach()

    foreach(item IN LISTS backend_definitions)
        if(item MATCHES "^\\$<")
            message(FATAL_ERROR "${BACKEND} 的 INTERFACE_COMPILE_DEFINITIONS 含生成器表达式：${item}")
        endif()
        if(MSVC)
            list(APPEND flags "/D${item}")
        else()
            list(APPEND flags "-D${item}")
        endif()
    endforeach()

    set(${OUT_FLAGS} ${flags} PARENT_SCOPE)
endfunction()

# 公共编译参数：只编译（/c），不链接；不做优化（负例只关心能否编译）
if(MSVC)
    set(MATH_VALIDATION_COMMON_FLAGS /nologo /std:c++20 /utf-8 /EHsc /c /W4)
else()
    set(MATH_VALIDATION_COMMON_FLAGS -std=c++20 -c -Wall -Wextra)
endif()

# 每个 backend 的用例清单：
#   合法对照 "<文件>|<标签>" 必须编译通过；
#   负例 "<文件>|<期望类别>" 必须编译失败，并且诊断里恰好 1 条静态断言 + 1 个 EXPECTED:<类别>。
set(MATH_VALIDATION_SCALAR_LEGAL
    "legal_control_scalar.cpp|legal-scalar")
set(MATH_VALIDATION_SCALAR_CASES
    "negative_scalar_int3_add.cpp|math.scalar.vector_int3_add_absent"
    "negative_scalar_float8_add.cpp|math.scalar.vector_float8_add_absent"
    "negative_scalar_cross_vec2.cpp|math.scalar.cross_vec2_absent"
    "negative_scalar_rvalue_subscript.cpp|math.scalar.rvalue_subscript_deleted"
    "negative_scalar_matmul_dimension.cpp|math.scalar.matmul_dimension_mismatch_absent")

set(MATH_VALIDATION_SIMD_LEGAL
    "legal_control_simd.cpp|legal-simd")
set(MATH_VALIDATION_SIMD_CASES
    "negative_simd_int3_add.cpp|math.simd.vector_int3_add_absent"
    "negative_simd_float8_add.cpp|math.simd.vector_float8_add_absent"
    "negative_simd_cross_vec2.cpp|math.simd.cross_vec2_absent"
    "negative_simd_rvalue_subscript.cpp|math.simd.rvalue_subscript_deleted"
    "negative_simd_matmul_dimension.cpp|math.simd.matmul_dimension_mismatch_absent")

set(MATH_VALIDATION_TARGETS "")
foreach(backend IN LISTS MATH_VALIDATION_BACKENDS)
    # 负例 TU 要包含伞头，因此除了插件自己的 PUBLIC 需求，还要拿到它依赖的 ID（Kernel/CoreTypes.hpp 等）
    set(backend_flags "")
    math_validation_collect_flags(${backend} backend_flags)
    if(TARGET ID)
        math_validation_collect_flags(ID backend_flags)
    endif()
    if(TARGET IDPluginStub)
        math_validation_collect_flags(IDPluginStub backend_flags)
    endif()
    set(validation_flags ${MATH_VALIDATION_COMMON_FLAGS} ${backend_flags})

    if(backend STREQUAL "IDMathScalar")
        set(token "scalar")
        set(legal_entries ${MATH_VALIDATION_SCALAR_LEGAL})
        set(case_entries  ${MATH_VALIDATION_SCALAR_CASES})
    else()
        set(token "simd")
        set(legal_entries ${MATH_VALIDATION_SIMD_LEGAL})
        set(case_entries  ${MATH_VALIDATION_SIMD_CASES})
    endif()

    set(target_name "IDMathNegativeCompileRun_${token}")
    add_custom_target(${target_name}
        COMMAND ${CMAKE_COMMAND}
            -DMATH_VALIDATION_COMPILER=${CMAKE_CXX_COMPILER}
            -DMATH_VALIDATION_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
            -DMATH_VALIDATION_DIR=${MATH_VALIDATION_DIR}
            -DMATH_VALIDATION_OUT=${MATH_VALIDATION_OUT}
            -DMATH_VALIDATION_WORK=${CMAKE_CURRENT_BINARY_DIR}/work-${token}
            -DMATH_VALIDATION_TOKEN=${token}
            "-DMATH_VALIDATION_FLAGS=${validation_flags}"
            "-DMATH_VALIDATION_LEGAL=${legal_entries}"
            "-DMATH_VALIDATION_CASES=${case_entries}"
            -P ${MATH_VALIDATION_DIR}/run_negative.cmake
        COMMENT "数学域负例编译校验（${token}）：合法对照必须通过，期望失败用例必须恰好 1 条静态断言 + EXPECTED 标记"
        VERBATIM
        USES_TERMINAL)
    list(APPEND MATH_VALIDATION_TARGETS ${target_name})
endforeach()

add_custom_target(IDMathNegativeCompileRun DEPENDS ${MATH_VALIDATION_TARGETS})
message(STATUS "Benchmark: 数学域负例驱动器 = IDMathNegativeCompileRun（backends=${MATH_VALIDATION_BACKENDS}）")
