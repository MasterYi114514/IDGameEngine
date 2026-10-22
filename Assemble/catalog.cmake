# 本源码/SDK 的完整 catalog：显式列出清单路径（计划书 8.1）。
#
# 条目 = 插件清单（plugin.json）路径；顺序不决定语义，也不决定构建输入或注册顺序。
# 这里只给路径，不重复插件名、依赖、候选组或能力：名字与依赖的唯一来源是各自的 plugin.json。
# 用显式路径而不是 glob；目录扫描最多只用于“清单是否漏登记”的校验。
#
# 新增插件时：在 `IDPlugin/<Plugin>/plugin.json` 写好清单，并在这里追加一行路径；
# 本文件不出现名字/依赖/能力，生成器里也没有任何插件特判。
# `Stub/` 是全局 new/delete 分配桩，不是插件，因此没有、也不应该有 plugin.json。
#
# fixture 不是业务插件，只用于证明组装流水线（单一清单 -> CMake 解析 -> C++ 编译期校验 -> 运行期绑定）成立；
# fixture_optional 的目标名刻意是 IDFixtureNotSelected：它的身份宏
# ID_USE_PLUGIN_IDFIXTURENOTSELECTED 正是 fixture_math 里“无关身份宏不得注入”那条 #error
# 检查的名字。默认 profile 不选中它（无宏路径），optional profile 选中它（有宏路径），
# 两条路径都是真实构建，检查因此不再是恒假的虚假保证。
set(ID_ASSEMBLY_CATALOG_MANIFESTS
    "${CMAKE_CURRENT_LIST_DIR}/tests/fixtures/fixture_api/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/fixtures/fixture_core/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/fixtures/fixture_math/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/fixtures/fixture_optional/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/../IDPlugin/IDLog/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/../IDPlugin/IDMathScalar/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/../IDPlugin/IDMathSIMD/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/../IDPlugin/IDWindow/plugin.json")

# 解析器负向用例清单（tests/rejections/**）：
#   这些条目本身是**合法的声明**，但它们的组合必然在 id_resolve_selection 阶段被拒绝
#   （AmbiguousProvider / DuplicateCapability / SlotConflict / RevisionMismatch），
#   所以永远不可能进入合法产品。只在 profile 名以 invalid- 开头时并入 catalog，
#   合法 profile 的 catalog 里没有任何"注定被拒绝"的条目。
#   负向用例只做 configure，不做构建：configure 在 add_subdirectory 之前就已失败，
#   因此这些目录不需要 CMakeLists.txt / include（它们不是可交付插件）。
set(ID_ASSEMBLY_REJECTION_MANIFESTS
    "${CMAKE_CURRENT_LIST_DIR}/tests/rejections/core_alt/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/rejections/ambiguous/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/rejections/duplicate_capability/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/rejections/slot_conflict/plugin.json"
    "${CMAKE_CURRENT_LIST_DIR}/tests/rejections/revision/plugin.json")

if(ID_ASSEMBLY_PROFILE MATCHES "^invalid-")
    list(APPEND ID_ASSEMBLY_CATALOG_MANIFESTS ${ID_ASSEMBLY_REJECTION_MANIFESTS})
endif()
