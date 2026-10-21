# module fixture（下游）：只交付源码与公开头
# 上游依赖不在这里写 DEPENDS：真实 PUBLIC 链接边与身份宏由解析结果统一接线（计划书 8.3）
id_assembly_add_module(IDFixtureMath
    SOURCES src/FixtureMathModule.cpp)
