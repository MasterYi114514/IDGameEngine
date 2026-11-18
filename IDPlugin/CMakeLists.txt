cmake_minimum_required(VERSION 4.1.0)

project(IDPlugin)

option(ID_ON_DEBUG "Define IDWINDOW_DEBUG for IDWindow: compile IDWindow with its debug-only code path" ON)

set(IDPLUGIN_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(IDPLUGIN_VENDOR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Vendor")

add_library(IDCommonHeaders INTERFACE)

target_include_directories(IDCommonHeaders
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/Common"
)

add_subdirectory(IDLog)
add_subdirectory(IDJson)
add_subdirectory(IDWindow)
