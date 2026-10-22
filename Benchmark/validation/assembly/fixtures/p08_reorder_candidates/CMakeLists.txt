cmake_minimum_required(VERSION 3.20)

# 组装 fixture：等价重排之三 —— 只打乱 **候选顺序**（OR 组内的书写顺序与 api_revisions 键顺序）。
#   候选顺序只影响诊断顺序，不参与匹配决策；因此 kProductFingerprint 必须与 p04 相等。
project(fixture_p08_reorder_candidates LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p08_reorder_candidates
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Or" "FX.Core" "FX.Alt")
