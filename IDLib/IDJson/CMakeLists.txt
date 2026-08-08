cmake_minimum_required(VERSION 3.10)

project(IDJson)
set(CMAKE_CXX_STANDARD 20)

set(IDJsonSupportIDMath ON)

file(GLOB_RECURSE IDJSON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDJson STATIC ${IDJSON_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/IDJson.hpp)

target_include_directories(IDJson 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IDLIB_DIR}/IDLog/include
)

target_link_directories(IDJson
    PRIVATE
        ${BIN_DIR}
)

target_link_libraries(IDJson
    PRIVATE
        IDLog
)

if(IDJsonSupportIDMath)
    target_compile_definitions(IDJson PUBLIC IDJSON_SUPPORT_IDMATH)
    target_include_directories(IDJson PUBLIC ${IDLIB_DIR}/IDMath/include)
    target_link_libraries(IDJson PUBLIC IDMath)
endif()