# ID.Window：名字/依赖/能力/槽位一律来自 catalog 读到的 plugin.json；
#
# GLFW 是本插件的**私有第三方**，以源码形式随插件构建（vendor/glfw 是完整 GLFW 源码树），
# 因此它不是上游插件：既不在 dependencies 里，也不产生 PUBLIC 边或身份宏。
# 静态库形态 + PRIVATE 链接的结果是：GLFW 的符号被链进本插件 DLL，它的 include 目录、
# GLFW_DLL 一类宏都不进入下游的编译单元；下游只能经 IDWindowApi.hpp 的 POD 函数表使用窗口能力。
#
# 下面这些变量的作用域只到本目录与 vendor/glfw 子树，不影响其它插件与内核：
set(GLFW_LIBRARY_TYPE              STATIC)   # 必须静态：出现第二个 GLFW DLL 就会有两份平台状态
set(GLFW_BUILD_DOCS                OFF)      # 文档/示例/测试不属于产品，不能进构建与安装树
set(GLFW_BUILD_EXAMPLES            OFF)
set(GLFW_BUILD_TESTS               OFF)
set(GLFW_INSTALL                   OFF)      # 关掉 GLFW 自己的 install/export 规则
if(MSVC)
    # GLFW 是 C 库，它的 MSVC_RUNTIME_LIBRARY 不受 id_apply_performance_flags 覆盖（那是逐插件的
    # C++ 目标函数），只能由 GLFW 自己的开关切到静态 CRT；否则同一个进程里会出现两份 CRT 状态。
    # 与 IDPluginStub 的约束同源：链接分配桩的模块必须内嵌同一套运行时。
    set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF)
endif()
# EXCLUDE_FROM_ALL：GLFW 只作为本插件的依赖被拉起，不进 all 目标
add_subdirectory(vendor/glfw EXCLUDE_FROM_ALL)

id_assembly_add_module(IDWindow
    SOURCES src/WindowModule.cpp)

# 第三方私有链接：位于 id_assembly_add_module 之后，因为 glfw 目标必须先存在。
# PRIVATE 是硬要求：GLFW 的头目录与宏只服务本插件的实现，不构成对下游的公开契约。
target_link_libraries(IDWindow PRIVATE glfw)
