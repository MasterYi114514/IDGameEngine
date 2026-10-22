cmake_minimum_required(VERSION 3.20)

# 组装 fixture：A→B→A 环只在 Selected 图上判定。
#   本 case 选中 FX.CycA 与 FX.CycB（互依赖），configure 期不报环（解析器不做拓扑排序），
#   C++ 侧的 sort_and_validate 必须按 PluginError::Cycle 拒绝 —— 由驱动的编译用例断言。
project(fixture_c01_cycle_selected LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    c01_cycle_selected
                 CATALOG "${ID_FIXTURE_FX_CYC_A}"
                         "${ID_FIXTURE_FX_CYC_B}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Cycle" "FX.CycA" "FX.CycB")
