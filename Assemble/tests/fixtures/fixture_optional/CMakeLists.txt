# module fixture（可选插件）：只交付源码与公开头；默认 profile 不选中它
# 名字/依赖/能力/槽位一律来自 catalog 读到的 plugin.json，这里不重复
id_assembly_add_module(IDFixtureNotSelected
    SOURCES src/FixtureOptionalModule.cpp)
