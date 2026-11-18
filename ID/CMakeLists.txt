cmake_minimum_required(VERSION 4.1.0)

project(ID)

file(GLOB_RECURSE IDCORE_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# 入口只属于可执行文件：IDCore 供外部消费者（Benchmark）链接，不带 main
list(REMOVE_ITEM IDCORE_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

file(GLOB_RECURSE ID_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDCore STATIC ${IDCORE_SRC_FILES})
add_executable(ID ${ID_SRC_FILES})

target_compile_features(IDCore
    PUBLIC
        cxx_std_20
)
target_compile_features(ID
    PRIVATE
        cxx_std_20
)

set_target_properties(IDCore PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    CXX_EXTENSIONS OFF
    LINKER_LANGUAGE CXX
)
set_target_properties(ID PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    CXX_EXTENSIONS OFF
)

target_include_directories(IDCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(ID
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(IDCore
    PRIVATE
        IDJson
        IDLog
)

target_link_libraries(ID
    PRIVATE
        IDLog
        IDJson
)
