cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SANDBOX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(Sandbox ${SANDBOX_SRC})
target_link_libraries(Sandbox PRIVATE ID IDPluginStub)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(Sandbox)
endif()
