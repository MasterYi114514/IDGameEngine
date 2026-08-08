cmake_minimum_required(VERSION 4.1.0)

project(Sandbox)

set(CMAKE_CXX_STANDARD 20)

# 宏定义
# add_definitions(-D_ID_USE_IMPL)      # 使用 ID 库的实现

set(EXECUTABLE_OUTPUT_PATH ${BIN_DIR})
add_executable(Sandbox
    Sandbox.cpp
)

target_compile_definitions(Sandbox
    PRIVATE
        ASSETS_PATH=\"${ASSETS_DIR}\"
)

target_include_directories(Sandbox 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ID_DIR}
        ${ID_DIR}/src
        ${IDLIB_DIR}/IDJson/include
        ${IDLIB_DIR}/IDLog/include
        ${IDLIB_DIR}/IDMath/include
        ${IDLIB_DIR}/IDRenderer/include
        ${IDLIB_DIR}/IDRenderer/src/Resource/Texture
        ${IDLIB_DIR}/IDWindow/include
)

target_link_directories(Sandbox PRIVATE ${BIN_DIR})
target_link_libraries(Sandbox 
    PRIVATE 
        ID
        IDMath
        IDJson
        IDLog
        IDRenderer
        IDWindow
)