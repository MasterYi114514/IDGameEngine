cmake_minimum_required(VERSION 3.20)

set(ID_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 1. 插件静态内存劫持桩 (IDPluginStub)：每个插件与宿主程序强制私链，将 new/delete 转发给微内核 ID::Memory
add_library(IDPluginStub STATIC ${ID_PLUGIN_DIR}/Stub/IDPluginStub.cpp)
# 分配桩本身只依赖内核公开头；IDPlugin/include 存在时才作为公开 include 暴露。
# 同样区分构建树/安装树，避免把源目录绝对路径写进导出的 target。
if(EXISTS "${ID_PLUGIN_DIR}/include")
    target_include_directories(IDPluginStub PUBLIC
        $<BUILD_INTERFACE:${ID_PLUGIN_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/IDPlugin>)
endif()
target_link_libraries(IDPluginStub PUBLIC ID)

# 1.1 C++ 运行时一致性：让链接本桩的模块内嵌同一套静态 C++ 运行时。
#     为什么需要：桩把模块内普通 operator new/delete 转发给内核分配器，而 C++ 标准库若仍来自
#     动态运行时（MinGW 的 libstdc++-6.dll / libgcc_s_*.dll、MSVC 的 /MD 版 UCRT + MSVC STL），
#     就会出现 std::string 等容器"分配走桩、释放走 CRT（malloc/free）"（或反向）的混用堆，
#     跨 DLL 直接损坏堆。本机实测：文件 sink 走 spdlog 内部 std::string 的安装消费探针在未加本项的
#     构建下稳定以 0xC0000374（HEAP CORRUPTION）终止，加本项后同一份探针退出 0 且落盘内容完整。
#     为什么放在这里：只有链接本桩的模块才存在这种混用（内核 libID 不链接桩，不得被无谓增大）。
#     传播方式按平台不同，不能一概而论：
#       - GNU/MinGW：`target_link_options(IDPluginStub INTERFACE ...)` 是真正的 usage requirement，
#         随 PRIVATE/PUBLIC 链接关系传播给插件 DLL、宿主 exe、Benchmark、IDAssemblySmoke 与安装消费方，
#         不需要在各处重复声明，唯一出处是本文件的 GNU 分支；
#       - MSVC：CMake **没有** `INTERFACE_MSVC_RUNTIME_LIBRARY` 属性，静态 CRT 无法作为 usage
#         requirement 从本桩传播，改由 `cmake/IDFlags.cmake` 的 `id_apply_performance_flags`
#         对每个目标设置 `MSVC_RUNTIME_LIBRARY`；本文件不重复设置，避免两份真源漂移。
#     代价：每个模块各自内嵌一份 C++ 运行时，产物体积增长、运行时不再共享（这是换取堆一致性的
#     必然成本，不是可选项）。
#     许可证：libstdc++ 静态链接受 GPLv3 + GCC Runtime Library Exception 约束；正式分发前
#     必须由项目/法务确认适用性。本条不构成任何法律结论。
#     验证范围：MinGW/GCC（本机 GCC 16.2.0）与 MSVC（19.52）两侧都经构建与运行验证（MSVC 含 SHARED 与 STATIC
#     归并两种模式）；四配置矩阵（MinGW Debug/Release、MSVC Debug/Release）已构建并运行，Debug 为 /MTd；
#     安装消费方的默认 /MT 由 cmake/IDGameEngineConfig.cmake.in 覆盖（消费方显式设置的值不被覆盖）；
#     Clang/非 Windows 未验证。
if(MSVC)
    # MSVC 的静态 CRT 归 cmake/IDFlags.cmake 的 id_apply_performance_flags 统一设置
    # （MSVC_RUNTIME_LIBRARY = /MT，Debug 为 /MTd）；本桩由该函数覆盖，此处不再单独设置。
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # MinGW/GCC（本机经构建与运行验证）：把 libstdc++ 与 libgcc 静态链进每个链接桩的模块。
    # 只影响链接期，不改变编译选项；libwinpthread 不在本选项覆盖范围内（保持动态）。
    target_link_options(IDPluginStub INTERFACE -static-libstdc++ -static-libgcc)
else()
    # Clang / Apple 等平台未验证：libc++ 与 libstdc++ 的静态链接参数不同（-static-libstdc++ 对 libc++ 无效），
    # 未验证的平台，需该平台另行确认后再补分支；这里不猜参数，也不让配置失败。
    message(STATUS "IDPluginStub: 静态 C++ 运行时未在 ${CMAKE_CXX_COMPILER_ID} 上验证，保持默认链接")
endif()

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
        target_compile_features(${PLUGIN_NAME} INTERFACE cxx_std_20)
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

    target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_20)
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

# 公用三方库所在路径
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty)
