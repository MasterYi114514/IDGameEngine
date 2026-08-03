cmake_minimum_required(VERSION 3.10)

project(IDJson)

file(GLOB_RECURSE IDJSON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDJson STATIC ${IDJSON_SRC})

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