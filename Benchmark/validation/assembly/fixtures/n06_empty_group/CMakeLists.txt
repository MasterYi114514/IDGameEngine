cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：需求组写成空数组（[]）。
#   期望 EmptyRequirementGroup：空组不是"没有上游"，要表达没有上游应当整组去掉。
project(fixture_n06_empty_group LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n06_empty_group
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Empty" "FX.Core")
