cmake_minimum_required(VERSION 4.1.0)

project(ID)
set(CMAKE_CXX_STANDARD 20)

# 选项
set(USE_IMGUI ON)          # 是否使用 ImGui
set(ID_SHARED_LIB ON)      # 是否编译为动态库（ON: 动态库 / OFF: 静态库）


set(ID_RENDERER "OpenGL" CACHE STRING "渲染后端: OpenGL, Vulkan, DirectX")

set(_VALID_RENDERERS OpenGL Vulkan DirectX)
list(FIND _VALID_RENDERERS "${ID_RENDERER}" _RENDERER_INDEX)
if(_RENDERER_INDEX EQUAL -1)
    message(FATAL_ERROR "ID_RENDERER 必须是以下之一: OpenGL / Vulkan / DirectX，当前值: ${ID_RENDERER}")
endif()

message(STATUS "ID 渲染后端: ${ID_RENDERER}")

# 常规宏定义

add_definitions(-D_ID_DEBUG)

if(ID_SHARED_LIB)
    add_definitions(-D_ID_SHARED_LIB)
endif()

# 源文件查找
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})

if(ID_SHARED_LIB)
    add_library(ID SHARED ${SRC_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/ID.hpp)
else()
    add_library(ID STATIC ${SRC_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/ID.hpp)
endif()

# DLL 导出宏（仅 ID 库自身需要）
target_compile_definitions(ID PRIVATE -DID_EXPORT)

target_precompile_headers(ID
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/IDpch.hpp
)

target_include_directories(ID
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IDLIB_DIR}/IDWindow/include
        ${IDLIB_DIR}/IDRenderer/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IDLIB_DIR}/IDLog/include
        ${IDLIB_DIR}/IDMath/include
        ${VENDOR_DIR}/glfw/include
        ${VENDOR_DIR}/glad/include
)


if(ID_RENDERER STREQUAL "OpenGL")
    target_compile_definitions(ID PUBLIC -DID_RENDER_OPENGL)

    target_link_directories(ID
        PRIVATE
            ${BIN_DIR}
    )
    target_link_libraries(ID
        PRIVATE
            glfw
            glad
            IDMath
            IDLog
            IDWindow
            IDRenderer
    )

elseif(ID_RENDERER STREQUAL "Vulkan")
    target_compile_definitions(ID PUBLIC -DID_RENDER_VULKAN)

    target_link_directories(ID
        PRIVATE
            ${BIN_DIR}
    )
    target_link_libraries(ID
        PRIVATE
            IDMath
            IDWindow
        PUBLIC
            IDLog
    )
    message(WARNING "Vulkan 后端尚未实现，编译通过但运行时会报错")

elseif(ID_RENDERER STREQUAL "DirectX")
    target_compile_definitions(ID PUBLIC -DID_RENDER_DIRECTX)

    target_link_directories(ID
        PRIVATE
            ${BIN_DIR}
    )
    target_link_libraries(ID
        PRIVATE
            IDMath
            IDWindow
        PUBLIC
            IDLog
    )
    message(WARNING "DirectX 后端尚未实现，编译通过但运行时会报错")
endif()

if(USE_IMGUI)
    target_compile_definitions(ID PUBLIC -DID_USE_IMGUI)
endif()
