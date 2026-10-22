cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：需求组内同名候选出现两次。
#   期望 DuplicateCandidate：候选集合是集合语义，重复书写不得被静默去重成"命中 1 个"。
project(fixture_n04_duplicate_candidate LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n04_duplicate_candidate
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Dup" "FX.Core")
