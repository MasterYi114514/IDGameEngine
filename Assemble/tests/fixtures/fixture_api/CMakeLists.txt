# interface（纯头文件）fixture：只有公开头，没有源文件、没有生命周期回调
# 名字/能力/槽位/依赖一律不在这里重复：全部来自 catalog 读到的 plugin.json（计划书 8.3）
id_assembly_add_module(IDFixtureApi)
