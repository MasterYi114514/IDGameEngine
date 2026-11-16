cmake_minimum_required(VERSION 4.1.0)

project(ID)

file(GLOB_RECURSE ID_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(ID ${ID_SRC_FILES})

target_compile_features(ID
    PRIVATE
        cxx_std_20
)

set_target_properties(ID PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    CXX_EXTENSIONS OFF
)

target_link_libraries(ID
    PRIVATE
        IDLog
        IDJson
)
