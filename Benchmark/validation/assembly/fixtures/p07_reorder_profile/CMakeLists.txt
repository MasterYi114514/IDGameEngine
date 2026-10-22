cmake_minimum_required(VERSION 3.20)

# 组装 fixture：等价重排之二 —— 只打乱 **profile 顺序**（enabled_plugins 书写顺序）。
#   选中集合与 p04 相同，因此 kProductFingerprint 必须与 p04 相等：
#   组语义标志按"选中槽位"编址，跨 TU 与跨顺序都必须还原成同一个 canonical 计划。
project(fixture_p07_reorder_profile LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FixtureProbe.cmake")

id_fixture_probe(NAME    p07_reorder_profile
                 CATALOG "${ID_FIXTURE_FX_CORE}"
                         "${ID_FIXTURE_FX_ALT}"
                         "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
                 ENABLED "FX.Alt" "FX.Core" "FX.Consumer.Or")
