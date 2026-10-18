cmake_minimum_required(VERSION 3.20)

project(IDGameEngine
    LANGUAGES CXX
    VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建模式选项 -----------------------------------------------------------
option(ID_MONOLITHIC_BUILD "静态归并所有插件并开启全程序优化 (LTO/LTCG)" OFF)
option(ID_ENABLE_AVX2      "开启 AVX2/FMA SIMD 指令集优化" ON)
option(ID_BUILD_BENCHMARK  "构建测试与性能基准" ON)
# -----------------------------------------------------------------------

# 可以自己设置 -----------------------------------------------------------
set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)                # 所有库文件与可执行文件的输出目录
# -----------------------------------------------------------------------

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR}/bin)

if(MSVC)
    # 移除 CMake 默认注入的 /EHsc 异常处理选项，避免与 /EHs-c- 冲突
    string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# 统一极限性能编译参数封装函数
function(id_apply_performance_flags TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /utf-8 /W4 /GR- /EHs-c-
            /wd4251 /wd4324
            $<$<CONFIG:Release>:/O2 /Oi /Ot /Ob3 /Gw /Gy /fp:fast>
        )
        if(ID_ENABLE_AVX2)
            target_compile_options(${TARGET_NAME} PRIVATE /arch:AVX2)
        endif()
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -fno-rtti -fno-exceptions
            $<$<CONFIG:Release>:-O3 -ffast-math -fomit-frame-pointer>
        )
        if(ID_ENABLE_AVX2)
            target_compile_options(${TARGET_NAME} PRIVATE -mavx2 -mfma)
        endif()
    endif()

    if(ID_MONOLITHIC_BUILD)
        set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

# 五层平级架构：ID（微内核）/ IDPlugin（插件）/ Sandbox（游戏本体）/ Benchmark（测试）/ Assemble（组装）
# 当前已落地 ID / IDPlugin / Sandbox / Benchmark；Assemble 尚未建立
add_subdirectory(ID)
add_subdirectory(IDPlugin)
add_subdirectory(Sandbox)
if(ID_BUILD_BENCHMARK)
    add_subdirectory(Benchmark)   # 测试与基准
endif()
