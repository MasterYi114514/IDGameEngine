#include "ProductionCaseSupport.hpp"

/**
 *  active 上限：128 个选中插件合法，129 个必须拒绝（计划书 6.5 / 16.1）
 *  合法对照：128 个 active、129 个 catalog 条目，真实组装并带证据校验。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr size_t kActive  = 128;
    inline constexpr size_t kCatalog = 129;
#else
    inline constexpr size_t kActive  = 129;
    inline constexpr size_t kCatalog = 129;
#endif

    inline constexpr auto kEntries = generated_leaf_tuple<kCatalog>();

    template <size_t... Index>
    consteval auto make_selected(std::index_sequence<Index...>) noexcept
    {
        return std::array<PluginIdentity, sizeof...(Index)>{ generated_identity(Index)... };
    }

    inline constexpr auto kSelected = make_selected(std::make_index_sequence<kActive>{});

    using Shape = ShapeOf<Plain<decltype(kEntries)>, kActive, kActive, 1, 1>;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_generated_leaf_evidence(make_input<Shape>(kEntries, kSelected)));
#else
    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kEntries, kSelected));
#endif

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "active_overflow: 128 active 必须合法");
    static_assert(kPlan.active_count == 128, "active_overflow: active_count 必须等于 128");
    static_assert(kPlan.provision_count == 128, "active_overflow: 128 个选中插件各有一条 provision");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, TooManyActive);
#endif
} // namespace
