cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：FX.Core 在 catalog 里，但 profile 没有选中它。
#   期望 MissingProvider：产品不自动补依赖，缺了就是失败，不静默降级。
project(fixture_n02_schema1_missing LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n02_schema1_missing
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Missing")
