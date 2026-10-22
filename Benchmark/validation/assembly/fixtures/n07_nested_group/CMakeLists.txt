cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：需求组里又套了一层数组（第三层）。
#   期望 NestedRequirementGroup：依赖只有外层 AND、内层 OR 两层，不允许嵌套组。
project(fixture_n07_nested_group LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n07_nested_group
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Nested" "FX.Core")
