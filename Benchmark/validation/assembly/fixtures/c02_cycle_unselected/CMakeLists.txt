cmake_minimum_required(VERSION 3.20)

# 组装 fixture（c01 的合法对照）：catalog 里存在互依赖的 FX.CycA/FX.CycB，
# 但本 profile 只选中 FX.Neutral，因此 Selected 图上没有环：
#   环只在选中集合上判定，"仅 OR 候选成环但未选中"不得被拒绝。
# 本 case 的 configure 与 C++ 校验 TU 都必须通过（c01 则必须按 Cycle 被拒）。
project(fixture_c02_cycle_unselected LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    c02_cycle_unselected
                 CATALOG "${ID_FIXTURE_FX_CYC_A}"
                         "${ID_FIXTURE_FX_CYC_B}"
                         "${ID_FIXTURE_FX_NEUTRAL}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.CycleFree" "FX.Neutral")
