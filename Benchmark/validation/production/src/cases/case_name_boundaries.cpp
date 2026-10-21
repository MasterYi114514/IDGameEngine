#include "ProductionCaseSupport.hpp"

/**
 *  名字长度界限：96 字节合法、97 字节必须拒绝（计划书 5.2）
 *  合法对照：96 字节名字真实进入 catalog/selected 并组装成功；16/17 字节跨前缀字边界也合法。
 */

namespace
{
    using namespace ID::Bench::Production;

    struct LongName
    {
        char     data[98] = {};
        uint32_t length   = 0;

        constexpr std::string_view view() const noexcept
        {
            return { data, length };
        }
    };

    consteval LongName make_long_name(size_t total) noexcept
    {
        LongName name{};
        name.data[0] = 'A';
        for (size_t i = 1; i < total; ++i)
        {
            name.data[i] = (i % 7 == 0) ? '.' : 'a';
        }
        name.data[total] = '\0';
        name.length      = static_cast<uint32_t>(total);
        return name;
    }

    inline constexpr LongName kName16 = make_long_name(16);
    inline constexpr LongName kName17 = make_long_name(17);
    inline constexpr LongName kName96 = make_long_name(kMaxNameBytes);
    inline constexpr LongName kName97 = make_long_name(kMaxNameBytes + 1);

    consteval PluginIdentity identity_of(const LongName& name) noexcept
    {
        return ID::PluginCompileTime::detail::make_identity(name.data, name.length);
    }

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry(identity_of(kName96), std::tuple<>{}, std::array<std::string_view, 0>{}, "IDCaseLong",
                   "ID::Case::Long", provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ identity_of(kName96) };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;

    consteval auto make_input_with_evidence() noexcept
    {
        auto input = make_input<Shape>(kCatalog, kSelected);
        input.evidence.selected[0]    = SelectedEvidence{ kName96.view(), "IDCaseLong",
                                                          "ID_USE_PLUGIN_IDCASELONG", identity_of(kName96).id };
        input.evidence.selected_count = 1;
        input.evidence.runtime[0]     = RuntimeBindingEvidence{ kName96.view(), kCapAlpha, identity_of(kName96).id, 1 };
        input.evidence.runtime_count  = 1;
        input.evidence.slots[0]       = SlotEvidence{ kName96.view(), kCapAlpha, 0 };
        input.evidence.slot_count     = 1;
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input_with_evidence());

    static_assert(identity_of(kName96).is_valid, "name_boundaries: 96 字节名字必须是合法身份");
    static_assert(identity_of(kName16).is_valid && identity_of(kName17).is_valid,
                  "name_boundaries: 16/17 字节名字必须合法");
    static_assert(!identity_of(kName97).is_valid, "name_boundaries: 97 字节名字必须被拒绝");
    static_assert(kPlan.diagnostic.code == PluginError::None, "name_boundaries: 96 字节名字必须组装成功");
    static_assert(kPlan.active_identity[0].length == kMaxNameBytes, "name_boundaries: 计划必须保留完整长度");
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(identity_of(kName97), std::tuple<>{}, std::array<std::string_view, 0>{}, "IDCaseLong",
                   "ID::Case::Long", provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ identity_of(kName97) };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan     = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    static_assert(identity_of(kName96).is_valid && !identity_of(kName97).is_valid,
                  "name_boundaries: 96/97 边界必须先成立");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
