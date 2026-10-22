cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：schema2 的字符串项 = 单项必需（等价于只含一个候选的组）。
#   两组分别是 FX.Core 与 FX.Alt，profile 只选中 FX.Core，第二组 0 命中。
#   期望 MissingProvider：AND 缺一必须失败，不得回退成"有哪个用哪个"。
project(fixture_n03_schema2_and_missing LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n03_schema2_and_missing
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.AndMissing" "FX.Core")
