# module 插件：只交付源码与公开头；名字/依赖/能力/槽位一律来自 catalog 读到的 plugin.json
# spdlog 按 header-only 与 IDLog 一起编译：不建第三方库目标、不定义 SPDLOG_COMPILED_LIB；
# vendor 路径只经 PRIVATE_INCLUDE_DIRS 进入本插件自身的编译，不向下游传播。
id_assembly_add_module(IDLog
    SOURCES src/IDLogModule.cpp src/SpdlogFrontend.cpp
    PRIVATE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include)
