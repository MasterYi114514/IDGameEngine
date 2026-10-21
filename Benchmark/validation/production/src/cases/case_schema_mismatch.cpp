#include "ProductionCaseSupport.hpp"

/**
 *  组装输入 schema 修订与头文件不一致：声明阶段直接拒绝（计划书 5.4 / 7）
 *  合法对照：使用当前 kSchemaRevision。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;

    consteval auto make_case_input() noexcept
    {
        auto input            = with_evidence(make_input<Shape>(kCatalog, kSelected));
#if defined(PRODUCTION_CASE_LEGAL)
        input.schema_revision = kSchemaRevision;
#else
        input.schema_revision = 0;      // 未知修订
#endif
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "schema_mismatch: 合法对照必须组装成功");
    static_assert(kPlan.schema_revision == kSchemaRevision, "schema_mismatch: 计划必须记录当前 schema 修订");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, SchemaMismatch);
#endif
} // namespace
