cmake_minimum_required(VERSION 3.20)

set(ID_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 1. 插件静态内存劫持桩 (IDPluginStub)：每个插件与宿主程序强制私链，将 new/delete 转发给微内核 ID::Memory
add_library(IDPluginStub STATIC ${ID_PLUGIN_DIR}/Stub/IDPluginStub.cpp)
target_include_directories(IDPluginStub PUBLIC ${ID_PLUGIN_DIR}/include)
target_link_libraries(IDPluginStub PUBLIC ID)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(IDPluginStub)
endif()

# 2. 通用插件构建原语：支持 INLINE_ONLY (Tier 1) 与 SHARED/STATIC LTO 双模构建 (Tier 2/3)
function(id_add_plugin PLUGIN_NAME)
    cmake_parse_arguments(PLUGIN "INLINE_ONLY" "" "SOURCES;DEPENDS" ${ARGN})

    if(PLUGIN_INLINE_ONLY)
        add_library(${PLUGIN_NAME} INTERFACE)
        target_include_directories(${PLUGIN_NAME} INTERFACE ${ID_PLUGIN_DIR}/include)
        target_link_libraries(${PLUGIN_NAME} INTERFACE ID ${PLUGIN_DEPENDS})
        return()
    endif()

    if(ID_MONOLITHIC_BUILD)
        add_library(${PLUGIN_NAME} STATIC ${PLUGIN_SOURCES})
        set_property(TARGET ${PLUGIN_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})
        target_compile_definitions(${PLUGIN_NAME} PRIVATE ID_PLUGIN_BUILD_DLL=1)
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC ${ID_PLUGIN_DIR}/include)
    if(COMMAND id_apply_performance_flags)
        id_apply_performance_flags(${PLUGIN_NAME})
    endif()
    target_link_libraries(${PLUGIN_NAME} PUBLIC ID ${PLUGIN_DEPENDS})
    target_link_libraries(${PLUGIN_NAME} PRIVATE IDPluginStub)
endfunction()
