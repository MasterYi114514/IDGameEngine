#include "ProductionCaseSupport.hpp"

/**
 *  catalog 上限：256 个条目合法，257 个必须拒绝（计划书 6.5）
 *  合法对照：256 个条目、只选中 1 个，真实组装并带证据校验。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr size_t kCatalog = 256;
#else
    inline constexpr size_t kCatalog = 257;
#endif

    inline constexpr auto kEntries  = generated_leaf_tuple<kCatalog>();
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ generated_identity(0) };

    using Shape = ShapeOf<Plain<decltype(kEntries)>, 1, 1, 1, 1>;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_generated_evidence(make_input<Shape>(kEntries, kSelected)));
#else
    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kEntries, kSelected));
#endif

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "catalog_overflow: 256 条 catalog 必须合法");
    static_assert(kPlan.catalog_count == 256, "catalog_overflow: catalog_count 必须等于 256");
    static_assert(kPlan.active_count == 1, "catalog_overflow: 只选中一个插件");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, TooManyCatalog);
#endif
} // namespace
