cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：需求组引用了 catalog 之外的候选名 FX.Ghost。
#   期望 UnknownCandidate：catalog 条目即使不被选中也会被校验，不存在"运行时再说"。
project(fixture_n08_unknown_candidate LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n08_unknown_candidate
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Unknown")
