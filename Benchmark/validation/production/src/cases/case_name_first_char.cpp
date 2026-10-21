#include "ProductionCaseSupport.hpp"

/**
 *  名字首字符必须是 ASCII 字母（计划书 5.2）
 *  合法对照：首字符为字母的同一名字。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(ID::PluginManager::register_plugin("1D.Case.Bad"), std::tuple<>{},
                   std::array<std::string_view, 0>{}, "IDCaseBadFirst", "ID::Case::BadFirst",
                   provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ ID::PluginManager::register_plugin("1D.Case.Bad") };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "name_first_char: 合法对照必须组装成功");
#else
    static_assert(!ID::PluginManager::register_plugin("1D.Case.Bad").is_valid,
                  "name_first_char: 数字首字符必须使身份非法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
