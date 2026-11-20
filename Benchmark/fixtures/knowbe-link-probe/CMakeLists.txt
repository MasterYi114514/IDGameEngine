cmake_minimum_required(VERSION 4.1.0)

project(knowbe_link_probe)

# Knowbe core 目录由调用方给出：夹具是独立工程，不猜盘符、不从工作目录推导。
# 用例：cmake -S <本目录> -B <构建目录> -G "MinGW Makefiles" -DKNOWBE_CORE_DIR=<工程根>/core
set(KNOWBE_CORE_DIR "" CACHE PATH "Knowbe core 目录（含 core/CMakeLists.txt）；必须由调用方显式给出")

if(NOT KNOWBE_CORE_DIR)
    message(FATAL_ERROR "KNOWBE_CORE_DIR 未给出：按 -DKNOWBE_CORE_DIR=<Knowbe 工程根>/core 重跑配置")
endif()

if(NOT EXISTS "${KNOWBE_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "KNOWBE_CORE_DIR 下没有 CMakeLists.txt：${KNOWBE_CORE_DIR}")
endif()

file(GLOB_RECURSE KNOWBE_LINK_PROBE_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(knowbe_link_probe ${KNOWBE_LINK_PROBE_SRC_FILES})

# 宿主 JSON 目标：独立夹具拿不到 Benchmark 宿主的 IDJson 目标，这里用最小 INTERFACE 目标让 core 的
# KNOWBE_JSON_TARGET 校验通过。这是**配置面探测，不代表真实 IDJson 链接**（core 在 P0 不引用 JSON 符号）。
add_library(IDJson INTERFACE)

# 日志里明写这是配置面探测，避免把本夹具的通过读成"真实 IDJson 链接已验证"
message(STATUS "knowbe-link-probe: IDJson 用最小 INTERFACE 目标（配置面探测，不代表真实 IDJson 链接）")

target_compile_features(knowbe_link_probe
    PRIVATE
        cxx_std_20
)

set_target_properties(knowbe_link_probe PROPERTIES
    CXX_EXTENSIONS OFF
)

# 探测 TU 要调用 core 的 Foundation 私有实现头：core 只把 include/ 作为 PUBLIC include 传播，
# core/src 是 PRIVATE。这条 include 根只属于夹具，不改 core 的对外 include。
target_include_directories(knowbe_link_probe
    PRIVATE
        "${KNOWBE_CORE_DIR}/src"
)

target_link_libraries(knowbe_link_probe
    PRIVATE
        KnowbeCore
)

# core 由宿主以普通 CMake 子目录接入：先注入 KNOWBE_JSON_TARGET 目标名，再 add_subdirectory(core)。
# 不 add_subdirectory 整个用户根、不 add_subdirectory(IDPlugin)、不引用 ID_OUTPUT_ROOT。
set(KNOWBE_JSON_TARGET IDJson)

add_subdirectory("${KNOWBE_CORE_DIR}" "${CMAKE_BINARY_DIR}/knowbe-core")
