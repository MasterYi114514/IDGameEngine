cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：需求组有 2 个候选，api_revisions 只写了 1 个。
#   期望 ContractRequirementMismatch：键集合必须与候选集合完全一致，缺条目不得按默认区间补齐。
project(fixture_n10_contract_missing LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n10_contract_missing
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.ContractMissing" "FX.Core" "FX.Alt")
