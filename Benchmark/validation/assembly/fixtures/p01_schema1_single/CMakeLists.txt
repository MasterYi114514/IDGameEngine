cmake_minimum_required(VERSION 3.20)

# 组装 fixture：schema1 单候选 -> configure 通过、1 条 matched、1 条边。
# 独立目录自带最小 CMake：临时 profile 由探针按 ENABLED 顺序生成（也可 -DID_FIXTURE_PROFILE 覆盖）。
# 手动运行：cmake -S <本目录> -B <任意 build 目录> -G "MinGW Makefiles"
project(fixture_p01_schema1_single LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p01_schema1_single
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Single" "FX.Core")
