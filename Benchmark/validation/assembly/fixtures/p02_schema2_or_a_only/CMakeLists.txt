cmake_minimum_required(VERSION 3.20)

# 组装 fixture：schema2 OR 组只命中 A（FX.Core）—— 1 条 matched、1 条边，组语义 allows_multi=1。
# 组级 capability = FX.Math 没有任何提供者：它只是契约标签，匹配真源是逐候选 capability。
project(fixture_p02_schema2_or_a_only LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p02_schema2_or_a_only
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Or" "FX.Core")
