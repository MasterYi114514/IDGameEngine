cmake_minimum_required(VERSION 3.20)

# 组装 fixture（反例）：api_revisions 里多出一条不属于本组候选的条目 FX.Alt。
#   期望 ContractCandidateMismatch：多余键同样破坏"键集合 == 候选集合"的对应关系。
project(fixture_n11_contract_extra LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    n11_contract_extra
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.ContractExtra" "FX.Core")
