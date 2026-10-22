cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：需求组把插件自己列为候选。
#   期望 SelfDependency：自依赖在解析阶段就被拒绝，不进入匹配（否则会得到一个自环）。
project(fixture_n05_self_dependency LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n05_self_dependency
                 CATALOG "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Self")
