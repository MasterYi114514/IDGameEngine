cmake_minimum_required(VERSION 3.15)

project(Benchmark)

# 测试源码用 CONFIGURE_DEPENDS 通配收集：新增域文件不需要手改本文件
file(GLOB_RECURSE BENCHMARK_SRC_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
)

add_executable(Benchmark ${BENCHMARK_SRC_FILES})

# `src` 也进 include 路径：域声明与测试私有头跟随实现在 src/<domain>/ 下（见 benchmark framework 的域声明约定）
target_include_directories(Benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 被测插件按普通库方式消费：公开头 + CMake 目标（不经过 ID 的生成接线，也不依赖运行期服务槽）
# IDJson 是 STATIC：公开头 + 目标直接链接；它自己的 IDLog 依赖以 LINK_ONLY 传递，这里不重复声明形式
# IDWindow 的链接形态由它自己的选项 IDWINDOW_BUILD_SHARED 决定，本文件不改该选项，只按现有目标链接
target_link_libraries(Benchmark
    PRIVATE
        IDCommonHeaders
        IDLog
        IDJson
        IDWindow
)

# 接入成本对照实现：直接调用同一份 vendor GLFW 的等价接线（os-baseline）。
# 只在 IDWindow 为 STATIC 时链接：SHARED 形态下 GLFW 被静态编入 IDWindow.dll，测试侧再链一份静态 GLFW
# 只会得到第二份未初始化的 GLFW 全局状态，因此那种形态下对照代码整体不参与编译（见 window_performance.cpp 的守卫）。
if(NOT IDWINDOW_BUILD_SHARED)
    target_link_libraries(Benchmark PRIVATE glfw)
endif()

target_compile_features(Benchmark
    PRIVATE
        cxx_std_20
)

# ---- 构建身份与路径注入：报告路径是绝对路径，与工作目录无关；缺失时实现侧显示 unknown ----
set(BENCHMARK_REPORT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/report.txt"
    CACHE FILEPATH "官方报告路径（绝对路径）")

# 链接形态按目标实际类型派生（同一构建树内的单一来源），三种被测插件并列写进报告头：
# 多个插件的形态不同，一个笼统的 SHARED/STATIC 会把形态写错
function(benchmark_link_form out_var target_name)
    if(TARGET ${target_name})
        get_target_property(_type ${target_name} TYPE)
        if(_type STREQUAL "STATIC_LIBRARY")
            set(${out_var} "STATIC" PARENT_SCOPE)
        elseif(_type STREQUAL "SHARED_LIBRARY")
            set(${out_var} "SHARED" PARENT_SCOPE)
        else()
            set(${out_var} "${_type}" PARENT_SCOPE)
        endif()
    else()
        set(${out_var} "unknown" PARENT_SCOPE)
    endif()
endfunction()

benchmark_link_form(BENCHMARK_IDWINDOW_FORM IDWindow)
benchmark_link_form(BENCHMARK_IDLOG_FORM IDLog)
benchmark_link_form(BENCHMARK_IDJSON_FORM IDJson)

set(BENCHMARK_LINK_MODE "IDWindow=${BENCHMARK_IDWINDOW_FORM},IDLog=${BENCHMARK_IDLOG_FORM},IDJson=${BENCHMARK_IDJSON_FORM}"
    CACHE STRING "被测插件的链接形态（报告头用；由目标实际类型派生）")

if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(BENCHMARK_LTO_VALUE "ON")
else()
    set(BENCHMARK_LTO_VALUE "OFF")
endif()

# 报告头的架构必须描述目标而不是主机：VS 的 win32 平台下 CMAKE_SYSTEM_PROCESSOR 仍是 AMD64，
# 直接写进去会把 32 位构建标成 AMD64（与 bits=32 自相矛盾），因此按目标指针宽度校正；
# 取不到目标架构标识时保留系统处理器并标注 32 位目标，不猜具体架构名
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BENCHMARK_ARCH_VALUE "${CMAKE_SYSTEM_PROCESSOR}")
elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    set(BENCHMARK_ARCH_VALUE "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
    set(BENCHMARK_ARCH_VALUE "${CMAKE_SYSTEM_PROCESSOR}/32bit")
endif()

# IDJson 错误日志桥的构建身份：取值直接来自插件自己的选项 IDJSON_ENABLE_LOG（同一构建树内唯一来源），
# 不复制第二份配置。用途见 include/benchmark_support.hpp 的 BENCHMARK_IDJSON_LOG 兜底注释。
if(IDJSON_ENABLE_LOG)
    set(BENCHMARK_IDJSON_LOG_VALUE "ON")
else()
    set(BENCHMARK_IDJSON_LOG_VALUE "OFF")
endif()

# window 域的编译器级负例取证需要"同一编译器、同一 include 根"：把它作为构建身份注入，
# 不在测试里猜编译器路径或 include 布局
if(MSVC)
    set(BENCHMARK_CXX_FAMILY_VALUE "MSVC")
else()
    set(BENCHMARK_CXX_FAMILY_VALUE "${CMAKE_CXX_COMPILER_ID}")
endif()

set(BENCHMARK_IDWINDOW_INCLUDE_VALUE "${CMAKE_SOURCE_DIR}/IDPlugin/IDWindow/include")
set(BENCHMARK_COMMON_INCLUDE_VALUE "${CMAKE_SOURCE_DIR}/IDPlugin/Common")

target_compile_definitions(Benchmark
    PRIVATE
        BENCHMARK_REPORT_PATH="${BENCHMARK_REPORT_FILE}"
        BENCHMARK_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
        BENCHMARK_BUILD_CONFIG="$<CONFIG>"
        BENCHMARK_LINK_MODE="${BENCHMARK_LINK_MODE}"
        BENCHMARK_LTO="${BENCHMARK_LTO_VALUE}"
        BENCHMARK_ARCH="${BENCHMARK_ARCH_VALUE}"
        BENCHMARK_IDJSON_LOG="${BENCHMARK_IDJSON_LOG_VALUE}"
        BENCHMARK_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
        BENCHMARK_CXX_FAMILY="${BENCHMARK_CXX_FAMILY_VALUE}"
        BENCHMARK_WINDOW_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
        BENCHMARK_IDWINDOW_INCLUDE="${BENCHMARK_IDWINDOW_INCLUDE_VALUE}"
        BENCHMARK_COMMON_INCLUDE="${BENCHMARK_COMMON_INCLUDE_VALUE}"
)

# IDLog 是 SHARED：把运行期 DLL 复制到测试可执行文件旁边，避免依赖 PATH 或工作目录
add_custom_command(TARGET Benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:IDLog>"
            "$<TARGET_FILE_DIR:Benchmark>"
    COMMENT "copy IDLog shared library next to the Benchmark executable"
)

# IDWindow 在 SHARED 形态下同样需要在运行期就位：报告里的 window.link.form_shared_dll_loaded
# 断言的是"运行期确实加载了 IDWindow.dll"，DLL 由本 POST_BUILD 放到 exe 旁边
if(IDWINDOW_BUILD_SHARED)
    add_custom_command(TARGET Benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:IDWindow>"
                "$<TARGET_FILE_DIR:Benchmark>"
        COMMENT "copy IDWindow shared library next to the Benchmark executable"
    )
endif()
