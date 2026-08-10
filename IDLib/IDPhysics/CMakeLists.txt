cmake_minimum_required(VERSION 4.1.0)

project(IDPhysics)
set(CMAKE_CXX_STANDARD 20)

add_definitions(-DIDPHYSICS_EXPORTS)

file(GLOB_RECURSE IDPHYSICS_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDPhysics SHARED ${IDPHYSICS_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/include/IDPhysics.hpp)

target_include_directories(IDPhysics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IDLIB_DIR}/IDLog/include
        ${VENDOR_DIR}/bullet3/src          # btBulletDynamicsCommon.h 所在目录
)

target_link_directories(IDPhysics
    PRIVATE
        ${BIN_DIR}
)

# IDMath 是 PUBLIC 依赖（ColliderShape/RigidBody 公开 API 使用 Vec3/Quat/Mat4）
# Bullet3 目标由 ThirdParty/CMakeLists.txt 的 add_subdirectory(bullet3) 提供
target_link_libraries(IDPhysics
    PUBLIC
        IDMath
    PRIVATE
        IDLog
        BulletDynamics
        BulletCollision
        LinearMath
)
