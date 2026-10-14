cmake_minimum_required(VERSION 3.20)

project(ID 
    LANGUAGES CXX
    VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可以自己设置 -----------------------------------------------------------
    set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../bin)             # 所有库文件的输出目录
# -----------------------------------------------------------------------


# 不要擅自修改 -----------------------------------------------------------
    set(ID_DIR ${CMAKE_CURRENT_SOURCE_DIR})
# -----------------------------------------------------------------------

file(GLOB_RECURSE ID_SRC ${ID_DIR}/include/*.hpp ${ID_DIR}/src/*.cpp)
add_library(ID SHARED ${ID_SRC})

target_include_directories(ID PRIVATE ${ID_DIR}/include)
target_compile_definitions(ID PRIVATE ID_KERNEL_BUILD_DLL)