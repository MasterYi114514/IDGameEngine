cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：契约里 min=0。
#   期望 InvalidRevision：接口修订从 1 起，0 不得被当成"任意版本"或"不用检查"。
project(fixture_n09_invalid_revision LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n09_invalid_revision
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Revision" "FX.Core")
