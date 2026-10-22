cmake_minimum_required(VERSION 3.20)

# 组装 fixture：schema2 OR 组只命中 B（FX.Alt）—— 1 条 matched、1 条边。
# 与 p02 同一份 consumer 清单、同一份 catalog：换 profile 就换命中，downstream 源码不动。
project(fixture_p03_schema2_or_b_only LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p03_schema2_or_b_only
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Or" "FX.Alt")
