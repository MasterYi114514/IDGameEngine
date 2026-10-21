cmake_minimum_required(VERSION 3.20)

# =============================================================================
# Sandbox：游戏本体宿主（第五层里的"游戏"）
#   - 只消费 `IDGameEngine` 产品目标：不登记插件描述符、不手工创建插件、不维护第二份依赖表；
#   - 启动/关闭只走 `ID::Engine::start()` / `ID::Engine::shutdown()`（计划书 9.3）；
#   - 不承担任何正确性测试或性能验证职责，那些全部归 Benchmark。
#   - 产品目标由 Assemble 层创建，因此构建宿主必须先设置 ID_ASSEMBLY_PROFILE。
# =============================================================================

if(NOT TARGET IDGameEngine)
    message(FATAL_ERROR
        "ID_BUILD_SANDBOX=ON 需要产品消费目标 IDGameEngine，但当前没有设置 ID_ASSEMBLY_PROFILE。\n"
        "二选一：\n"
        "  1) 设置产品：-DID_ASSEMBLY_PROFILE=<Assemble/profiles/名字>（如 default / zero；presets dev / dev-mono 已内置）；\n"
        "  2) 不构建宿主：-DID_BUILD_SANDBOX=OFF（默认值）。")
endif()

file(GLOB_RECURSE SANDBOX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(Sandbox ${SANDBOX_SRC})
target_link_libraries(Sandbox PRIVATE IDGameEngine)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(Sandbox)
endif()

# SHARED 模式：Windows/MinGW 的 DLL 查找从 exe 所在目录开始，所以把内核与已选中插件模块
# 复制到宿主旁边；只复制有实体产物的目标（INTERFACE 插件没有产物）。
# STATIC 归并时全部符号都在 exe 内，不需要复制。
if(NOT ID_MONOLITHIC_BUILD)
    add_custom_command(TARGET Sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ID>"
                "$<TARGET_FILE_DIR:Sandbox>"
        COMMENT "copy ID kernel runtime next to Sandbox")

    get_property(product_runtime_targets GLOBAL PROPERTY ID_PRODUCT_RUNTIME_TARGETS)
    foreach(runtime_target IN LISTS product_runtime_targets)
        add_custom_command(TARGET Sandbox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${runtime_target}>"
                    "$<TARGET_FILE_DIR:Sandbox>"
            COMMENT "copy selected plugin ${runtime_target} next to Sandbox")
    endforeach()
endif()
