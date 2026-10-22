# 共享 support 清单的路径表（组装 fixture 的上游提供者）
#
# 这里只登记路径，不重复名字/能力/依赖：元数据的唯一来源仍是各自的 plugin.json。
# 每个 case 的 CMakeLists.txt 用这些变量拼自己的 catalog，避免在多份 fixture 里抄写相对路径。
#
# 全部 support 清单都是 `kind=interface` + `lifecycle=none`（纯声明、无源文件、无槽位）：
# 本资产只做 configure 期解析/选择/生成与编译期静态校验，不创建插件 target、不产出二进制。

set(ID_FIXTURE_SUPPORT_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(ID_FIXTURE_FX_CORE     "${ID_FIXTURE_SUPPORT_DIR}/fx_core/plugin.json")
set(ID_FIXTURE_FX_CORE_ALT "${ID_FIXTURE_SUPPORT_DIR}/fx_core_alt/plugin.json")
set(ID_FIXTURE_FX_ALT      "${ID_FIXTURE_SUPPORT_DIR}/fx_alt/plugin.json")
set(ID_FIXTURE_FX_VIA      "${ID_FIXTURE_SUPPORT_DIR}/fx_via/plugin.json")
set(ID_FIXTURE_FX_CYC_A    "${ID_FIXTURE_SUPPORT_DIR}/fx_cyc_a/plugin.json")
set(ID_FIXTURE_FX_CYC_B    "${ID_FIXTURE_SUPPORT_DIR}/fx_cyc_b/plugin.json")
set(ID_FIXTURE_FX_NEUTRAL  "${ID_FIXTURE_SUPPORT_DIR}/fx_neutral/plugin.json")

foreach(_fixture_support_manifest
        "${ID_FIXTURE_FX_CORE}" "${ID_FIXTURE_FX_CORE_ALT}" "${ID_FIXTURE_FX_ALT}"
        "${ID_FIXTURE_FX_VIA}" "${ID_FIXTURE_FX_CYC_A}" "${ID_FIXTURE_FX_CYC_B}"
        "${ID_FIXTURE_FX_NEUTRAL}")
    if(NOT EXISTS "${_fixture_support_manifest}")
        message(FATAL_ERROR "组装 fixture 的 support 清单缺失：${_fixture_support_manifest}")
    endif()
endforeach()
