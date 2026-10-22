cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：schema1 同一需求组里两个候选都提供同一能力 FX.Core，且两者都被选中。
#   期望 configure 期按 AmbiguousProvider 拒绝：schema1 要求组内恰好命中 1 个，
#   不会被静默改判为合法（那需要迁到 schema_version=2）。
project(fixture_n01_schema1_ambiguous LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n01_schema1_ambiguous
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_CORE_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Ambiguous" "FX.Core" "FX.CoreAlt")
