cmake_minimum_required(VERSION 3.20)

# 不要擅自修改 -----------------------------------------------------------
set(ID_DIR ${CMAKE_CURRENT_SOURCE_DIR})
# -----------------------------------------------------------------------

file(GLOB_RECURSE ID_KERNEL_SRC ${ID_DIR}/include/*.hpp ${ID_DIR}/src/Kernel/*.cpp)

if(ID_MONOLITHIC_BUILD)
    add_library(ID STATIC ${ID_KERNEL_SRC})
    target_compile_definitions(ID PUBLIC ID_STATIC_PLUGIN_LINK=1)
else()
    add_library(ID SHARED ${ID_KERNEL_SRC})
    target_compile_definitions(ID PRIVATE ID_KERNEL_BUILD_DLL=1)
endif()

target_include_directories(ID PUBLIC ${ID_DIR}/include)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(ID)
endif()
