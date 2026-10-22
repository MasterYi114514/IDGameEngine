cmake_minimum_required(VERSION 3.20)

# 组装 fixture：schema2 OR 组同时命中 A+B —— 2 条 matched、2 条边，不是 AmbiguousProvider。
# 本 case 是等价重排对照的基准（p06/p07/p08 必须与它得到同一个 kProductFingerprint）。
project(fixture_p04_schema2_or_both LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p04_schema2_or_both
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Or" "FX.Core" "FX.Alt")
