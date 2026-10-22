cmake_minimum_required(VERSION 3.20)

# 组装 fixture：等价重排之一 —— 只打乱 **catalog 顺序**（清单枚举顺序）。
#   语义与 p04 完全相同（同一份清单、同一组选中集合），因此 kProductFingerprint 必须与 p04 相等。
project(fixture_p06_reorder_catalog LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p06_reorder_catalog
                 CATALOG "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                         "${ID_FIXTURE_FX_CORE}"
                 ENABLED "FX.Consumer.Or" "FX.Core" "FX.Alt")
