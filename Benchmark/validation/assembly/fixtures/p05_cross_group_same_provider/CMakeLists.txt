cmake_minimum_required(VERSION 3.20)

# 组装 fixture：跨组重复候选 —— 两个需求组都命中 FX.Core。
#   期望 bindings=2（每个 (下游, 需求组, 命中 provider) 一条 matched 记录）
#        edges=1   （真实 PUBLIC 边按 (上游, 下游) 去重，多组指向同一上游只连一次）
#   因此 binding 证据行数 > edge 行数，且 canonical-binding 不会因为边去重而丢一条。
project(fixture_p05_cross_group_same_provider LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p05_cross_group_same_provider
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Cross" "FX.Core")
