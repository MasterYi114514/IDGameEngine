cmake_minimum_required(VERSION 4.1.0)

project(IDWindow)

option(IDWINDOW_BUILD_SHARED "Build IDWindow as a shared library" OFF)

set(IDWINDOW_GLFW_DIR "${IDPLUGIN_VENDOR_PATH}/glfw-3.5.1")
if(IDWINDOW_BUILD_SHARED)
    set(IDWINDOW_LIBRARY_TYPE SHARED)
else()
    set(IDWINDOW_LIBRARY_TYPE STATIC)
endif()
if(NOT EXISTS "${IDWINDOW_GLFW_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "找不到 vendor GLFW：${IDWINDOW_GLFW_DIR}")
endif()
if(TARGET glfw)
    message(FATAL_ERROR "glfw 目标已存在：GLFW 必须且只能由 IDWindow 接入")
endif()

file(GLOB_RECURSE IDWINDOW_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDWindow ${IDWINDOW_LIBRARY_TYPE} ${IDWINDOW_SRC_FILES})

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

target_compile_features(IDWindow
    PUBLIC
        cxx_std_20
)

set_target_properties(IDWindow PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    CXX_EXTENSIONS OFF
)

target_include_directories(IDWindow
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(IDWindow
    PUBLIC
        IDCommonHeaders
    PRIVATE
        glfw
        IDLog
)

add_subdirectory("${IDWINDOW_GLFW_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)
