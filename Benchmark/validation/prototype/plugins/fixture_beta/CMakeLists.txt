# 逐插件构建入口：只描述本模块的源码与 include，不声明名字/依赖/角色（计划书 8.3）
id_fixture_add_module(IDFixtureBeta
    MODULE  Beta
    SOURCES src/FixtureBetaModule.cpp)

# 产物落到原型自己的 out/ 目录，不写根工程共用的 bin
idf_isolate_outputs(IDFixtureBeta)
