# =============================================================================
# 构建开关与编译参数（计划书 8.1 / 13.2）
#
# 本文件只回答"怎么编译"：性能/诊断编译参数、IPO 探测与开关。
# 它**不**参与插件选择：catalog、profile、逐需求 provider、身份宏与链接边一律由
# Assemble/** 的解析器派生，禁止在这里出现任何具体插件名或产品开关。
#
# 由根 CMakeLists.txt 在 project() 之后 include；各层在调用前用
# `if(COMMAND id_apply_performance_flags)` 守卫，保证单独把子目录当子工程用时不会中断。
# =============================================================================
include_guard()

# MSVC 默认注入的 /EHsc 会与 /EHs-c- 冲突：在配置期从全局编译参数里剥掉
if(MSVC)
    string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

include(CheckIPOSupported)

# MinGW/GCC 的 IPO 目标会被 ar 归档，而 MinGW 的 ar 没有 LTO 插件：
# 它会打印 "plugin needed to handle lto object" 并生成符号索引不完整的归档。
# 工具链自带的 gcc-ar / gcc-ranlib 是同一工具链的包装器，这里只在"用户没有显式换过归档器"时改用它，
# 且只在编译器目录里确实存在时切换，不猜路径、不写死盘符（计划书 13.2 的可移植要求）。
if(ID_MONOLITHIC_BUILD AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(id_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(id_ar_name "${CMAKE_AR}" NAME_WE)
    if(id_ar_name STREQUAL "ar")
        # 只在编译器自己的 bin 目录里找，不搜索 PATH 之外的其它位置
        find_program(id_gcc_ar     NAMES gcc-ar     HINTS "${id_cxx_compiler_dir}" NO_DEFAULT_PATH)
        find_program(id_gcc_ranlib NAMES gcc-ranlib HINTS "${id_cxx_compiler_dir}" NO_DEFAULT_PATH)
        if(id_gcc_ar AND id_gcc_ranlib)
            # 同时写 cache 与当前作用域变量：CMP0126 下 set(CACHE) 不会覆盖已存在的普通变量，
            # 而各目标真正读取的是作用域里的普通变量（cache 只负责在 CMakeCache 里留下痕迹）。
            set(CMAKE_AR     "${id_gcc_ar}"     CACHE FILEPATH "归档器（IPO 需要 LTO 插件）" FORCE)
            set(CMAKE_RANLIB "${id_gcc_ranlib}" CACHE FILEPATH "归档索引器（IPO 需要 LTO 插件）" FORCE)
            set(CMAKE_AR     "${id_gcc_ar}")
            set(CMAKE_RANLIB "${id_gcc_ranlib}")
            message(STATUS "ID: IPO 归档器 = ${CMAKE_AR} / 索引器 = ${CMAKE_RANLIB}")
        else()
            message(STATUS "ID: 未找到 gcc-ar/gcc-ranlib，IPO 归档仍用 ${CMAKE_AR}（可能缺少 LTO 插件）")
        endif()
    endif()
endif()

# 一次性探测 IPO（LTO/LTCG）支持；结果记忆在 cache 变量里，重复调用不重复探测、也不重复刷屏。
# 探测口径是"本工具链能否编译并链接一个 IPO 目标"，不等于承诺全程序内联。
function(id_detect_ipo_support)
    if(DEFINED ID_IPO_SUPPORTED)
        return()
    endif()
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output LANGUAGES CXX)
    set(ID_IPO_SUPPORTED "${ipo_supported}" CACHE INTERNAL "IPO(LTO/LTCG) 是否被当前工具链支持")
    set(ID_IPO_MESSAGE   "${ipo_output}"    CACHE INTERNAL "IPO 探测输出")
    message(STATUS "ID: IPO 支持 = ${ID_IPO_SUPPORTED}（${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}）")
endfunction()

# 对目标开启 IPO：编译与最终链接都要带上 LTO/LTCG，
# 只给静态库设置属性并不构成全程序优化承诺（计划书 13.2）。
# 用法：id_apply_ipo(<target>)
function(id_apply_ipo TARGET_NAME)
    id_detect_ipo_support()
    if(NOT ID_IPO_SUPPORTED)
        return()
    endif()
    set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endfunction()

# 统一极限性能编译参数封装函数（Release 才上的强度优化用生成器表达式隔离）
# 用法：id_apply_performance_flags(<target>)
function(id_apply_performance_flags TARGET_NAME)
    if(MSVC)
        # 静态 CRT：/MT（Debug 为 /MTd）。它与 IDPlugin/CMakeLists.txt 的 MinGW 分支
        # （-static-libstdc++ -static-libgcc）是同一条全局约束：链接分配桩 IDPluginStub 的模块
        # 必须内嵌同一套 C++ 运行时，否则模块内的 operator new/delete 与 STL 容器会落到不同的堆，
        # 释放时损坏堆。
        # 只能逐目标设置：CMake 没有 INTERFACE_MSVC_RUNTIME_LIBRARY 属性，静态 CRT 无法像
        # MinGW 的 INTERFACE 链接选项那样作为 usage requirement 从 IDPluginStub 传播，
        # 因此所有目标统一经本函数设置；副作用是内核 ID 也用 /MT，但它不链接分配桩。
        # 需要 CMP0091=NEW（根 CMakeLists.txt 的 cmake_minimum_required(VERSION 3.21) 已满足）；
        # 属性只对 MSVC ABI 生效，非 MSVC 平台不会因此配置失败。
        set_property(TARGET ${TARGET_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
        id_detect_ipo_support()
        if(ID_IPO_SUPPORTED)
            id_apply_ipo(${TARGET_NAME})
        elseif(NOT ID_IPO_WARNED)
            # 显式告警一次：mono 模式承诺静态归并 + IPO，工具链不支持时不能静默当成已开启
            set(ID_IPO_WARNED TRUE CACHE INTERNAL "IPO 不可用的告警是否已输出")
            message(WARNING
                "ID_MONOLITHIC_BUILD=ON 但 ${CMAKE_CXX_COMPILER_ID} 不支持 IPO："
                "本构建只有静态归并，没有全程序优化（${ID_IPO_MESSAGE}）")
        endif()
    endif()
endfunction()
