#include "ProductionCaseSupport.hpp"

/**
 *  非 NUL 终止的字符数组不是合法身份，也不能按 N-1 静默截掉最后一个字符（计划书 5.2）
 *  合法对照：同一前缀的 NUL 终止名字。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr char kNoNul[5] = { 'I', 'D', '.', 'A', 'B' };      // 末尾不是 '\0'

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(ID::PluginManager::register_plugin(kNoNul), std::tuple<>{}, std::array<std::string_view, 0>{},
                   "IDCaseNoNul", "ID::Case::NoNul", provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ ID::PluginManager::register_plugin(kNoNul) };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "name_unterminated: 合法对照必须组装成功");
#else
    static_assert(!ID::PluginManager::register_plugin(kNoNul).is_valid,
                  "name_unterminated: 非终止数组必须得到无效身份");
    static_assert(ID::PluginManager::register_plugin(kNoNul).length == 5,
                  "name_unterminated: 无效身份必须保留全部 5 个输入字节，不能静默截断");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
