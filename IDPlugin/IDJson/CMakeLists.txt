cmake_minimum_required(VERSION 4.1.0)

project(IDJson)

option(IDJSON_ENABLE_LOG "Enable the IDLog error-log bridge inside IDJson" ON)

file(GLOB_RECURSE IDJSON_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDJson STATIC ${IDJSON_SRC_FILES})

target_compile_definitions(IDJson
    PUBLIC
        ID_JSON_STATIC
)
if(IDJSON_ENABLE_LOG)
    target_compile_definitions(IDJson
        PRIVATE
            ID_JSON_ENABLE_LOG=1
    )
endif()
if(NOT IDJSON_ENABLE_LOG)
    target_compile_definitions(IDJson
        PRIVATE
            ID_JSON_ENABLE_LOG=0
    )
endif()

target_compile_features(IDJson
    PUBLIC
        cxx_std_20
)

set_target_properties(IDJson PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    CXX_EXTENSIONS OFF
)

target_include_directories(IDJson
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(IDJson
    PUBLIC
        IDCommonHeaders
)
if(IDJSON_ENABLE_LOG)
    target_link_libraries(IDJson
        PRIVATE
            IDLog
    )
endif()
