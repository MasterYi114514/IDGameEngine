cmake_minimum_required(VERSION 4.1.0)

project(IDLog)

file(GLOB_RECURSE IDLOG_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDLog SHARED ${IDLOG_SRC_FILES})

target_compile_definitions(IDLog
    PRIVATE
        ID_LOG_EXPORTS
)

target_compile_features(IDLog
    PUBLIC
        cxx_std_20
)

set_target_properties(IDLog PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    CXX_EXTENSIONS OFF
)

target_include_directories(IDLog
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(IDLog
    SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include"
)

target_link_libraries(IDLog
    PUBLIC
        IDCommonHeaders
)
