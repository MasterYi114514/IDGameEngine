cmake_minimum_required(VERSION 3.20)

set(ID_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 1. 插件静态内存劫持桩 (IDPluginStub)：每个插件与宿主程序强制私链，将 new/delete 转发给微内核 ID::Memory
add_library(IDPluginStub STATIC ${ID_PLUGIN_DIR}/Stub/IDPluginStub.cpp)
target_include_directories(IDPluginStub PUBLIC ${ID_PLUGIN_DIR}/include)
target_link_libraries(IDPluginStub PUBLIC ID)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(IDPluginStub)
endif()

# 2. 通用插件构建原语：
#    - 支持每个插件拥有独立文件夹 (IDPlugin/<PluginName>)、独立 INCLUDE_DIR 与独立导出宏 (BUILD_MACRO)
#    - 自动向插件自身及所有依赖它的下游注入 PUBLIC 身份宏 ID_USE_PLUGIN_<UPPER_NAME>=1（供下游用 #if 差异化兼容）
#    - 支持 ROLE_ALIAS 将当前选中的插件绑定到抽象角色目标（如 ROLE_ALIAS ID::Math），实现一键切换不同插件后端
function(id_add_plugin PLUGIN_NAME)
    cmake_parse_arguments(PLUGIN "INLINE_ONLY" "BUILD_MACRO;INCLUDE_DIR;ROLE_ALIAS" "SOURCES;DEPENDS" ${ARGN})

    string(TOUPPER "${PLUGIN_NAME}" PLUGIN_UPPER_NAME)
    string(REGEX REPLACE "[^A-Z0-9_]" "_" PLUGIN_UPPER_NAME "${PLUGIN_UPPER_NAME}")
    set(PLUGIN_IDENTITY_MACRO "ID_USE_PLUGIN_${PLUGIN_UPPER_NAME}=1")

    if( NOT PLUGIN_INCLUDE_DIR )
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
            set(PLUGIN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
        else()
            set(PLUGIN_INCLUDE_DIR "${ID_PLUGIN_DIR}/include")
        endif()
    endif()

    if(PLUGIN_INLINE_ONLY)
        add_library(${PLUGIN_NAME} INTERFACE)
        target_include_directories(${PLUGIN_NAME} INTERFACE ${PLUGIN_INCLUDE_DIR})
        target_compile_definitions(${PLUGIN_NAME} INTERFACE ${PLUGIN_IDENTITY_MACRO})
        target_link_libraries(${PLUGIN_NAME} INTERFACE ID ${PLUGIN_DEPENDS})
        if(PLUGIN_ROLE_ALIAS)
            add_library(${PLUGIN_ROLE_ALIAS} ALIAS ${PLUGIN_NAME})
        endif()
        return()
    endif()

    if(ID_MONOLITHIC_BUILD)
        add_library(${PLUGIN_NAME} STATIC ${PLUGIN_SOURCES})
        set_property(TARGET ${PLUGIN_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})
        target_compile_definitions(${PLUGIN_NAME} PRIVATE ID_PLUGIN_BUILD_DLL=1)
        if(PLUGIN_BUILD_MACRO)
            target_compile_definitions(${PLUGIN_NAME} PRIVATE ${PLUGIN_BUILD_MACRO}=1)
        else()
            target_compile_definitions(${PLUGIN_NAME} PRIVATE ID_${PLUGIN_UPPER_NAME}_BUILD_DLL=1)
        endif()
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC ${PLUGIN_INCLUDE_DIR})
    target_compile_definitions(${PLUGIN_NAME} PUBLIC ${PLUGIN_IDENTITY_MACRO})

    if(COMMAND id_apply_performance_flags)
        id_apply_performance_flags(${PLUGIN_NAME})
    endif()

    target_link_libraries(${PLUGIN_NAME} PUBLIC ID ${PLUGIN_DEPENDS})
    target_link_libraries(${PLUGIN_NAME} PRIVATE IDPluginStub)

    if(PLUGIN_ROLE_ALIAS)
        add_library(${PLUGIN_ROLE_ALIAS} ALIAS ${PLUGIN_NAME})
    endif()
endfunction()
