cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：schema_version=1 的清单里把 dependencies 项写成字符串（schema2 形态）。
#   期望 ManifestTypeMismatch：schema1 的每项必须是候选数组；混写不会被"尽力解析"成合法输入。
project(fixture_n12_type_mismatch LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n12_type_mismatch
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.TypeMismatch" "FX.Core")
