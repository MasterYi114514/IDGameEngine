cmake_minimum_required(VERSION 3.20)

# 组装 fixture：传递身份宏 vs 组 matched 集合的虚假阳性。
#   真实 PUBLIC 链 IDFxConsumerFp -> IDFxVia -> IDFxCore 让 consumer 的 TU 同时看到
#   ID_USE_PLUGIN_IDFXVIA 与 ID_USE_PLUGIN_IDFXCORE，但 FX.Core 不是 consumer 第 0 组的 matched 成员
#   （该组候选只有 FX.Via）。驱动的两个编译用例：
#     正向对照：ID_MATCHED_MEMBER_<C>_G0_<VIA> 存在、_<CORE> 不存在 —— 必须编译通过；
#     负向用例：断言 ID_MATCHED_MEMBER_<C>_G0_<CORE> 存在 —— 必须编译失败且恰好 1 条 EXPECTED 标记。
project(fixture_m01_transitive_identity LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    m01_transitive_identity
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_VIA}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Consumer.Fp" "FX.Via" "FX.Core")
