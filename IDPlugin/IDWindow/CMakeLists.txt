cmake_minimum_required(VERSION 3.15)

project(IDWindow)

# vendor GLFW 3.5.1 的最低要求是 3.16：低于此版本时先给出可读的原因，
# 否则报错会落在 add_subdirectory 内部
if(CMAKE_VERSION VERSION_LESS 3.16)
    message(FATAL_ERROR "IDWindow 需要 CMake >= 3.16（vendor GLFW 3.5.1 的最低要求），当前 ${CMAKE_VERSION}")
endif()

# 静态库是默认形态：GLFW 直接编入本目标，消费者不需要额外链接项；
# SHARED 形态下 GLFW 符号仍不进入导出面（vendor 侧 -fvisibility=hidden）
option(IDWINDOW_BUILD_SHARED "Build IDWindow as a shared library" OFF)

set(IDWINDOW_GLFW_DIR "${IDPLUGIN_VENDOR_PATH}/glfw-3.5.1")
if(NOT EXISTS "${IDWINDOW_GLFW_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "找不到 vendor GLFW：${IDWINDOW_GLFW_DIR}")
endif()
if(TARGET glfw)
    message(FATAL_ERROR "glfw 目标已存在：GLFW 必须且只能由 IDWindow 接入")
endif()
add_subdirectory("${IDWINDOW_GLFW_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)

if(IDWINDOW_BUILD_SHARED)
    set(IDWINDOW_LIBRARY_TYPE SHARED)
else()
    set(IDWINDOW_LIBRARY_TYPE STATIC)
endif()

file(GLOB_RECURSE IDWINDOW_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDWindow ${IDWINDOW_LIBRARY_TYPE} ${IDWINDOW_SRC_FILES})

# SHARED：库自身以导出身份编译，消费者按 dllimport 声明；STATIC：公开头按"无导入/导出"编译。
# 两个宏互斥，且都由本目标传递，消费者不需要自行定义
if(IDWINDOW_BUILD_SHARED)
    target_compile_definitions(IDWindow
        PRIVATE
            ID_WINDOW_EXPORTS
    )
else()
    target_compile_definitions(IDWindow
        PUBLIC
            ID_WINDOW_STATIC
    )
endif()

if(ID_ON_DEBUG)
    target_compile_definitions(IDWindow
        PRIVATE
            IDWINDOW_DEBUG
    )
endif()

target_include_directories(IDWindow
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# GLFW 只出现在 src/ 里：公开头不含任何 GLFW 头，消费者不需要 GLFW 的 include 路径
target_link_libraries(IDWindow
    PUBLIC
        IDCommonHeaders
    PRIVATE
        glfw
        IDLog
)

target_compile_features(IDWindow
    PUBLIC
        cxx_std_20
)

set_target_properties(IDWindow
    PROPERTIES
        CXX_EXTENSIONS OFF
)
