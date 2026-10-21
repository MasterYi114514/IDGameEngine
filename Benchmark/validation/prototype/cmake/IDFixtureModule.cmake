# fixture 模块构建原语
# 1. 每个模块只接收自己的源码，不接收名字/依赖/角色；
# 2. 逐模块生成独立导出头（STATIC 为空宏，SHARED 只由本模块 build 宏控制）；
# 3. 每模块私链 IDPluginStub，使本模块的普通 new/delete 路由到内核；
# 4. SHARED 模式下 build 宏为 PRIVATE，绝不向下游传播。
include_guard()

# id_fixture_add_module(<target> MODULE <名字> SOURCES <源文件...>)
function(id_fixture_add_module TARGET)
    cmake_parse_arguments(M "" "MODULE" "SOURCES" ${ARGN})

    if(NOT M_MODULE)
        message(FATAL_ERROR "id_fixture_add_module(${TARGET}): MODULE 必填")
    endif()

    string(TOUPPER "${M_MODULE}" MODULE_UPPER)

    # 逐插件导出头：只有本模块自己的 build 宏能打开 dllexport
    set(IDF_MODULE_UPPER "${MODULE_UPPER}")
    configure_file("${IDF_TEMPLATE_DIR}/PluginExport.generated.hpp.in"
                   "${IDF_GEN_DIR}/Fixture${M_MODULE}Export.generated.hpp"
                   @ONLY)

    if(ID_MONOLITHIC_BUILD)
        add_library(${TARGET} STATIC ${M_SOURCES})
    else()
        add_library(${TARGET} SHARED ${M_SOURCES})
        target_compile_definitions(${TARGET} PRIVATE ID_FIXTURE_${MODULE_UPPER}_BUILD_DLL=1)
    endif()

    target_include_directories(${TARGET} PUBLIC
        "${IDF_INCLUDE_DIR}"
        "${IDF_GEN_DIR}")
    target_link_libraries(${TARGET} PUBLIC ID)

    # 分配桩保留：普通归档链接只在"本模块真的引用了 operator new"时才抽取桩对象，
    # 模块从不分配时 DLL 里就没有替换符号（本目录实测过这一现象）。
    # 计划书 9.4 允许"该桩专用的链接保留机制"，因此这里只对 IDPluginStub 做 whole-archive，
    # 不对任何插件库做全量保留。
    if(MSVC)
        # /WHOLEARCHIVE 只是链接器选项，不产生库依赖：vcxproj 里只有 AdditionalOptions，
        # 既没有 IDPluginStub 的库项也没有 ProjectReference，干净构建目录单独构建本模块会得到
        # LNK1356（找不到指定为已整体存档的库，已实测复现）。
        # 所以先登记真实库依赖（建立构建顺序，并把桩纳入增量重链接的输入），再叠加整档保留选项。
        # 整档保留强度不变，且仍然只作用于 IDPluginStub，不对任何插件库做全量保留。
        target_link_libraries(${TARGET} PRIVATE IDPluginStub)
        target_link_options(${TARGET} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:IDPluginStub>")
    else()
        target_link_libraries(${TARGET} PRIVATE "-Wl,--whole-archive" IDPluginStub "-Wl,--no-whole-archive")
    endif()

    if(COMMAND id_apply_performance_flags)
        id_apply_performance_flags(${TARGET})
    endif()
endfunction()

# 把解析结果接线到已存在的目标上：PUBLIC 上游 + PUBLIC 身份宏（不注入无关身份）
function(id_fixture_apply_wiring)
    foreach(record IN LISTS ARGN)
        string(REPLACE "|" ";" parts "${record}")
        list(GET parts 0 dependent_target)
        list(GET parts 1 provider_target)
        list(GET parts 2 provider_token)
        if(NOT TARGET ${dependent_target} OR NOT TARGET ${provider_target})
            message(FATAL_ERROR "接线失败：目标不存在 ${dependent_target} <- ${provider_target}")
        endif()
        target_link_libraries(${dependent_target} PUBLIC ${provider_target})
        target_compile_definitions(${dependent_target} PUBLIC ID_USE_PLUGIN_${provider_token}=1)
    endforeach()
endfunction()
