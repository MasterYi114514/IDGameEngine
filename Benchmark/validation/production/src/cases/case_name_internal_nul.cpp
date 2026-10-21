#include "ProductionCaseSupport.hpp"

/**
 *  名字内部出现 NUL：必须 InvalidName，不能把 NUL 当成名字结束悄悄截断（计划书 5.2）
 *  合法对照：没有内嵌 NUL 的同一前缀名字。
 */

namespace
{
    using namespace ID::Bench::Production;

    // 13 个可见字符 + 内嵌 NUL + 4 个可见字符 = 18 字节，再加末尾 NUL
    inline constexpr char kNameInternalNul[19] = "ID.Case.Alpha\0Beta";

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#else
    inline constexpr auto kCatalog  = std::tuple{
        make_entry(ID::PluginManager::register_plugin(kNameInternalNul), std::tuple<>{},
                   std::array<std::string_view, 0>{}, "IDCaseInternalNul", "ID::Case::InternalNul",
                   provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ ID::PluginManager::register_plugin(kNameInternalNul) };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

    static_assert(kNameInternalNul[13] == '\0' && kNameInternalNul[17] == 'a',
                  "name_internal_nul: 夹具必须包含内嵌 NUL 且其后仍有可见字符");

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "name_internal_nul: 合法对照必须组装成功");
#else
    static_assert(!ID::PluginManager::register_plugin(kNameInternalNul).is_valid,
                  "name_internal_nul: 内嵌 NUL 必须使身份非法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
