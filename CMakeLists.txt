cmake_minimum_required(VERSION 4.1.0)

project(OpenGL)

set(BIN_DIR ${CMAKE_SOURCE_DIR}/bin)
set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/ThirdParty)
set(IDLIB_DIR ${CMAKE_SOURCE_DIR}/IDLib)
set(ID_DIR ${CMAKE_SOURCE_DIR}/ID)
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/Assets)

# 三方库
add_subdirectory(ThirdParty)

# ID库目录
add_subdirectory(IDLib)

# 项目目录
add_subdirectory(ID)
add_subdirectory(Sandbox)