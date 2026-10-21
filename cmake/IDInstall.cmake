# =============================================================================
# 安装与导出（计划书 13.4）
#
# 只在启用产品组装（ID_ASSEMBLY_PROFILE 非空）时由根 CMakeLists.txt include：
# 交付物必须绑定一个明确的产品，不能把 catalog 里的全部插件混进同一个产品。
#
# 交付内容：
#   1. 内核 `ID`、分配桩 `IDPluginStub`、组装库 `IDAssembly` 与全部选中插件模块；
#   2. 公开能力头（内核、各插件的 include/）、生成的选中身份与逐插件导出头；
#   3. 当前产品清单（`ProductManifest.txt`，即 CMake 派生的构建证据）与 profile；
#   4. `IDGameEngineConfig.cmake` + 可重定位的导出 target。
# SHARED 模式下 Windows 的 DLL 进 bin/，导入库/静态库进 lib/。
#
# 可重定位性来自各 target 的 INSTALL_INTERFACE：本文件不写任何源码目录绝对路径。
# =============================================================================
include_guard()

include(CMakePackageConfigHelpers)

get_filename_component(id_engine_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

get_property(id_install_assembly_name    GLOBAL PROPERTY ID_PRODUCT_ASSEMBLY_NAME)
get_property(id_install_selected_targets GLOBAL PROPERTY ID_PRODUCT_SELECTED_TARGETS)
get_property(id_install_generated_dir    GLOBAL PROPERTY ID_PRODUCT_GENERATED_DIR)
get_property(id_install_profile_path     GLOBAL PROPERTY ID_PRODUCT_PROFILE_PATH)
get_property(id_install_plugin_includes  GLOBAL PROPERTY ID_PRODUCT_PLUGIN_INCLUDE_DIRS)

if(NOT id_install_assembly_name OR NOT id_install_generated_dir)
    message(FATAL_ERROR "IDInstall: 组装层没有登记产品信息（assembly 名或生成目录为空）")
endif()

set(ID_INSTALL_EXPORT_SET      "IDGameEngineTargets")
set(ID_INSTALL_CMAKE_DIR       "${CMAKE_INSTALL_LIBDIR}/cmake/IDGameEngine")
set(ID_INSTALL_ASSEMBLY_DIR    "${CMAKE_INSTALL_DATADIR}/IDGameEngine/assemblies/${id_install_assembly_name}")
set(ID_INSTALL_GENERATED_INC   "${CMAKE_INSTALL_INCLUDEDIR}/IDGameEngine/generated/${id_install_assembly_name}")

# ---- 1. 内核与分配桩 --------------------------------------------------------
install(TARGETS ID IDPluginStub
    EXPORT   ${ID_INSTALL_EXPORT_SET}
    RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(DIRECTORY "${id_engine_dir}/ID/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ID"
    FILES_MATCHING PATTERN "*.hpp")

if(EXISTS "${id_engine_dir}/IDPlugin/include")
    install(DIRECTORY "${id_engine_dir}/IDPlugin/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/IDPlugin"
        FILES_MATCHING PATTERN "*.hpp")
endif()

# ---- 2. 组装库、产品消费目标与选中插件 --------------------------------------
install(TARGETS IDAssembly
    EXPORT   ${ID_INSTALL_EXPORT_SET}
    RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(TARGETS IDGameEngine EXPORT ${ID_INSTALL_EXPORT_SET})

foreach(plugin_target IN LISTS id_install_selected_targets)
    get_target_property(plugin_kind ${plugin_target} TYPE)
    if(plugin_kind STREQUAL "INTERFACE_LIBRARY")
        # 纯头文件插件：只有公开头与身份宏，没有二进制产物
        install(TARGETS ${plugin_target} EXPORT ${ID_INSTALL_EXPORT_SET})
    else()
        install(TARGETS ${plugin_target}
            EXPORT   ${ID_INSTALL_EXPORT_SET}
            RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
endforeach()

# 逐插件公开能力头：目录来自 catalog 的清单位置，不在这里重复插件名
foreach(include_entry IN LISTS id_install_plugin_includes)
    string(REPLACE "|" ";" include_parts "${include_entry}")
    list(GET include_parts 0 include_target)
    list(GET include_parts 1 include_dir)
    if(IS_DIRECTORY "${include_dir}")
        install(DIRECTORY "${include_dir}/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/IDGameEngine/plugins/${include_target}"
            FILES_MATCHING PATTERN "*.hpp")
    endif()
endforeach()

# ---- 3. 生成物：选中身份、逐插件导出头、产品清单与 profile -------------------
install(DIRECTORY "${id_install_generated_dir}/"
    DESTINATION "${ID_INSTALL_GENERATED_INC}"
    FILES_MATCHING PATTERN "*.hpp")

install(FILES "${id_install_generated_dir}/IDAssemblyEvidence.txt"
    DESTINATION "${ID_INSTALL_ASSEMBLY_DIR}"
    RENAME "ProductManifest.txt")

install(FILES "${id_install_profile_path}"
    DESTINATION "${ID_INSTALL_ASSEMBLY_DIR}"
    RENAME "product-profile.json")

# 产品消费入口头（ID::Engine::start / shutdown）
install(DIRECTORY "${id_engine_dir}/Assemble/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/IDGameEngine"
    FILES_MATCHING PATTERN "*.hpp")

# ---- 4. 包配置与导出 target -------------------------------------------------
set(ID_INSTALL_PRODUCT_NAME  "${id_install_assembly_name}")
if(ID_MONOLITHIC_BUILD)
    set(ID_INSTALL_PRODUCT_MODE "STATIC")
else()
    set(ID_INSTALL_PRODUCT_MODE "SHARED")
endif()

configure_package_config_file(
    "${id_engine_dir}/cmake/IDGameEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/IDGameEngineConfig.cmake"
    INSTALL_DESTINATION "${ID_INSTALL_CMAKE_DIR}")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/IDGameEngineConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/IDGameEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/IDGameEngineConfigVersion.cmake"
    DESTINATION "${ID_INSTALL_CMAKE_DIR}")

install(EXPORT ${ID_INSTALL_EXPORT_SET}
    FILE "IDGameEngineTargets.cmake"
    DESTINATION "${ID_INSTALL_CMAKE_DIR}")

message(STATUS "assembly[${id_install_assembly_name}] install: ${CMAKE_INSTALL_PREFIX}")
