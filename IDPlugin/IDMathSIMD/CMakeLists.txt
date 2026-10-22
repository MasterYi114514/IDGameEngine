# interface（纯头文件）插件：只有公开头，没有源文件、没有生命周期回调、没有运行期服务槽位。
# 名字 / 能力 / 修订 / 依赖一律不在这里重复：全部来自 catalog 读到的 plugin.json。
# 传 SOURCES 或 PRIVATE_INCLUDE_DIRS 会被 id_assembly_add_module 直接 FATAL 拒绝（纯头插件没有可编译的源）。
# ISA 要求（x64 SSE2）不在这里写编译选项：构建层统一传播 flags，头文件里的 ISA 门负责在不满足时编译期失败。
id_assembly_add_module(IDMathSIMD)
