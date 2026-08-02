cmake_minimum_required(VERSION 4.1.0)

project(IDMath)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(USE_GLM ON)                             # 底层是否使用 GLM 实现，如果为 OFF，则使用自定义实现
set(SHARED_LIBRARY OFF)                     # 仅在 BUILD_LIB 为 ON 时有效，是否编译为动态库库，如果为 OFF，则编译为静态库

if(USE_GLM)
    add_definitions(-DIDMATH_USE_GLM)      # 定义宏 IDMath_USE_GLM，表示使用 GLM 实现
endif()

file(
    GLOB_RECURSE IDMATH_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

if(SHARED_LIBRARY)
    set(LIBRARY_TYPE SHARED)
else()
    set(LIBRARY_TYPE STATIC)
endif()

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDMath ${LIBRARY_TYPE} ${IDMATH_SRC_FILES})

# 将宏传播给链接了 IDMath 的消费者
if(USE_GLM)
    target_compile_definitions(IDMath PUBLIC -DIDMATH_USE_GLM)
endif()

if(SHARED_LIBRARY)
    target_compile_definitions(IDMath PRIVATE -DIDMATH_EXPORTS)   # 动态库：仅自身 dllexport
else()
    target_compile_definitions(IDMath PUBLIC -DIDMATH_STATIC)     # 静态库：自身+消费者均为空
endif()

target_include_directories(IDMath
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IDLIB_DIR}/IDLog/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_directories(IDMath
    PRIVATE
        ${BIN_DIR}
)

target_link_libraries(IDMath
    PRIVATE
        IDLog
)