cmake_minimum_required(VERSION 3.20)

# 不要擅自修改 -----------------------------------------------------------
set(ID_DIR ${CMAKE_CURRENT_SOURCE_DIR})
# -----------------------------------------------------------------------

file(GLOB_RECURSE ID_KERNEL_SRC ${ID_DIR}/src/Kernel/*.hpp ${ID_DIR}/src/Kernel/*.cpp)

if(ID_MONOLITHIC_BUILD)
    add_library(ID STATIC ${ID_KERNEL_SRC})
    target_compile_definitions(ID PUBLIC ID_STATIC_PLUGIN_LINK=1)
else()
    add_library(ID SHARED ${ID_KERNEL_SRC})
    target_compile_definitions(ID PRIVATE ID_KERNEL_BUILD_DLL=1)
endif()

# C++20 是内核公开头的跨 DLL 源码契约：必须是 target 级 PUBLIC 要求，下游随之至少 20、工具链不支持时在 generate 期失败，而不是依赖根目录 CMAKE_CXX_STANDARD 的隐式继承
target_compile_features(ID PUBLIC cxx_std_20)

# 公开 include 必须区分构建树与安装树：把构建期的绝对路径写进导出 target 会让安装产物不可重定位
target_include_directories(ID PUBLIC
    $<BUILD_INTERFACE:${ID_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ID>)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(ID)
endif()
