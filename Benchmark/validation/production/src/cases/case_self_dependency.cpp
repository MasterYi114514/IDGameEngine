#include "ProductionCaseSupport.hpp"

/**
 *  自依赖：候选名包含所属插件自身时必须拒绝（计划书 5.3 / 16.1）
 *  合法对照：Alpha 依赖 Beta（两个不同插件），同一 TU、同一 include、同一编译参数。
 *  @note 生产 API 的自依赖判定必须用完整名字相等：只比较哈希或前 16 字节会把
 *        "同哈希不同名"误拒成自依赖，那类输入由 case_hash_collision 单独覆盖。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ beta(), alpha() };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;

    inline constexpr auto kInput = with_evidence(make_input<Shape>(kCatalog, kSelected));
    inline constexpr auto kPlan  = ID::PluginManager::sort_and_validate(kInput);

    static_assert(kPlan.diagnostic.code == PluginError::None, "self_dependency: 合法对照必须组装成功");
    static_assert(kPlan.active_count == 2, "self_dependency: 合法对照必须保留两个 active");
    static_assert(kPlan.edge_count == 1, "self_dependency: 合法对照只有一条真实边");
    static_assert(kPlan.active_identity[kPlan.find_active("ID.Case.Alpha")].id == alpha().id,
                  "self_dependency: canonical active 名字必须保留完整身份");
#else
    inline constexpr auto kCatalog  = std::tuple{ entry_dep(alpha(), kNameAlpha) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    // 自依赖是声明期错误：先证明身份本身合法，再要求类别
    static_assert(alpha().is_valid, "self_dependency: 夹具里的名字必须是合法身份");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, SelfDependency);
#endif
} // namespace
