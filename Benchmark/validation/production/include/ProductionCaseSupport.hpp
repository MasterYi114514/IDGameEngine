#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "Kernel/Plugin.hpp"

/**
 *  P1 生产 API 校验用例的公共地基（Benchmark/validation/production，不是生产代码）
 *  1. 只包含生产头 `Kernel/Plugin.hpp`，直接使用 `ID::PluginManager` 的三个静态 consteval 操作族
 *     与 `ID::PluginCompileTime` 的公开数据类型；不使用 P0 的 `ID::Bench::Fixture` 替身 API；
 *  2. 所有声明/元数据都是**拥有存储**的编译期对象：名字复制进 PluginIdentity / 声明字节池，
 *     string_view 只指向字符串字面量或本头里的静态存储表；
 *  3. 构建证据在这里由用例输入确定性派生（见 with_evidence）：只为小规模合法对照服务，
 *     真正独立的 CMake 证据链不在 P1 范围（JSON 组装尚未建立，不能声称已闭环）；
 *  4. 期望失败用例的互斥断言对见文末宏：实际类别 == 期望类别时只失败 error_differs 一条，
 *     消息带 ID_ASSEMBLY_ERROR_EXPECTED；类别不符时只失败 error_matches 一条，消息带
 *     ID_ASSEMBLY_ERROR_MISMATCH。驱动器要求前者出现、后者不出现且失败断言恰好 1 条。
 */

namespace ID::Bench::Production
{
    using ID::PluginCompileTime::AssemblyEvidence;
    using ID::PluginCompileTime::AssemblyInput;
    using ID::PluginCompileTime::AssemblyResult;
    using ID::PluginCompileTime::AssemblyShape;
    using ID::PluginCompileTime::CandidateRevision;
    using ID::PluginCompileTime::PluginCatalogEntry;
    using ID::PluginCompileTime::PluginDeclaration;
    using ID::PluginCompileTime::PluginError;
    using ID::PluginCompileTime::PluginIdentity;
    using ID::PluginCompileTime::PluginKind;
    using ID::PluginCompileTime::PluginLifecycle;
    using ID::PluginCompileTime::PluginMetadata;
    using ID::PluginCompileTime::Provision;
    using ID::PluginCompileTime::RequirementContract;
    using ID::PluginCompileTime::RuntimeBindingEvidence;
    using ID::PluginCompileTime::SelectedEvidence;
    using ID::PluginCompileTime::SlotEvidence;
    using ID::PluginCompileTime::kMaxNameBytes;
    using ID::PluginCompileTime::kMaxRequirements;
    using ID::PluginCompileTime::kMaxSlots;
    using ID::PluginCompileTime::kMacroCapacity;
    using ID::PluginCompileTime::kNoIndex;
    using ID::PluginCompileTime::kNoSlot;
    using ID::PluginCompileTime::kSchemaRevision;

    // ---- 固定插件名（合法 ASCII 标识；首字符均为字母） ----
    inline constexpr char kNameAlpha[]   = "ID.Case.Alpha";
    inline constexpr char kNameBeta[]    = "ID.Case.Beta";
    inline constexpr char kNameGamma[]   = "ID.Case.Gamma";
    inline constexpr char kNameDelta[]   = "ID.Case.Delta";
    inline constexpr char kNameEpsilon[] = "ID.Case.Epsilon";
    inline constexpr char kNameZeta[]    = "ID.Case.Zeta";
    inline constexpr char kNameEta[]     = "ID.Case.Eta";
    inline constexpr char kNameTheta[]   = "ID.Case.Theta";
    inline constexpr char kNameGhost[]   = "ID.Case.Ghost";       // 只出现在候选里，不进 catalog

    // ---- 固定目标名 / 命名空间 / 能力名 ----
    inline constexpr char kTargetAlpha[]   = "IDCaseAlpha";
    inline constexpr char kTargetBeta[]    = "IDCaseBeta";
    inline constexpr char kTargetGamma[]   = "IDCaseGamma";
    inline constexpr char kTargetDelta[]   = "IDCaseDelta";
    inline constexpr char kTargetEpsilon[] = "IDCaseEpsilon";
    inline constexpr char kTargetZeta[]    = "IDCaseZeta";
    inline constexpr char kTargetEta[]     = "IDCaseEta";
    inline constexpr char kTargetTheta[]   = "IDCaseTheta";

    inline constexpr char kNamespaceAlpha[]   = "ID::Case::Alpha";
    inline constexpr char kNamespaceBeta[]    = "ID::Case::Beta";
    inline constexpr char kNamespaceGamma[]   = "ID::Case::Gamma";
    inline constexpr char kNamespaceDelta[]   = "ID::Case::Delta";
    inline constexpr char kNamespaceEpsilon[] = "ID::Case::Epsilon";
    inline constexpr char kNamespaceZeta[]    = "ID::Case::Zeta";
    inline constexpr char kNamespaceEta[]     = "ID::Case::Eta";
    inline constexpr char kNamespaceTheta[]   = "ID::Case::Theta";

    inline constexpr char kCapAlpha[]   = "ID.Case.CapA";
    inline constexpr char kCapBeta[]    = "ID.Case.CapB";
    inline constexpr char kCapGamma[]   = "ID.Case.CapC";
    inline constexpr char kCapDelta[]   = "ID.Case.CapD";
    inline constexpr char kCapEpsilon[] = "ID.Case.CapE";
    inline constexpr char kCapZeta[]    = "ID.Case.CapF";
    inline constexpr char kCapEta[]     = "ID.Case.CapG";
    inline constexpr char kCapTheta[]   = "ID.Case.CapH";

    consteval PluginIdentity alpha() noexcept   { return ID::PluginManager::register_plugin(kNameAlpha); }
    consteval PluginIdentity beta() noexcept    { return ID::PluginManager::register_plugin(kNameBeta); }
    consteval PluginIdentity gamma() noexcept   { return ID::PluginManager::register_plugin(kNameGamma); }
    consteval PluginIdentity delta() noexcept   { return ID::PluginManager::register_plugin(kNameDelta); }
    consteval PluginIdentity epsilon() noexcept { return ID::PluginManager::register_plugin(kNameEpsilon); }
    consteval PluginIdentity zeta() noexcept    { return ID::PluginManager::register_plugin(kNameZeta); }
    consteval PluginIdentity eta() noexcept     { return ID::PluginManager::register_plugin(kNameEta); }
    consteval PluginIdentity theta() noexcept   { return ID::PluginManager::register_plugin(kNameTheta); }

    // ---- 需求组（内层 OR）：显式元素类型，避免 const char* 指针身份比较 ----
    consteval std::array<std::string_view, 0> group0() noexcept
    {
        return {};
    }

    consteval std::array<std::string_view, 1> group1(std::string_view first) noexcept
    {
        return { first };
    }

    consteval std::array<std::string_view, 2> group2(std::string_view first, std::string_view second) noexcept
    {
        return { first, second };
    }

    consteval std::array<std::string_view, 3> group3(std::string_view first, std::string_view second,
                                                     std::string_view third) noexcept
    {
        return { first, second, third };
    }

    namespace detail
    {
        template <typename Array, typename Group>
        constexpr void append_group_names(Array& out, size_t& cursor, const Group& group) noexcept
        {
            for (size_t i = 0; i < group.size(); ++i)
            {
                out[cursor++] = group[i];
            }
        }

        /**
         *  @brief 把 tuple-of-arrays 的叶子名字按组序摊平
         *  @return 长度 = 候选总数的 string_view 数组；每个 view 指向调用方传入的
         *          **静态存储名字**（字面量或 kPool），不指向声明内部的字节池
         *  @note 修订记录必须使用这些 view，不能用 declaration.candidate_view()：
         *        后者指向条目自身的 candidate_pool，条目复制/移动后即成悬空 view。
         */
        template <typename... Groups, size_t... Index>
        constexpr auto flatten_group_names_impl(const std::tuple<Groups...>& groups,
                                               std::index_sequence<Index...>) noexcept
        {
            constexpr size_t kCandidates = (ID::PluginCompileTime::detail::group_size<Groups>::value + ... + 0);
            std::array<std::string_view, kCandidates> names{};
            size_t                                    cursor = 0;
            ((append_group_names(names, cursor, std::get<Index>(groups))), ...);
            static_cast<void>(cursor);
            return names;
        }

        template <typename... Groups>
        constexpr auto flatten_group_names(const std::tuple<Groups...>& groups) noexcept
        {
            return flatten_group_names_impl(groups, std::index_sequence_for<Groups...>{});
        }
    } // namespace detail

    // ---- provides（扁平能力声明） ----
    template <size_t Count, size_t... Index>
    consteval auto repeated_groups(std::string_view candidate, std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ (static_cast<void>(Index), group1(candidate))... };
    }

    template <size_t Count>
    consteval auto repeated_groups(std::string_view candidate) noexcept
    {
        return repeated_groups<Count>(candidate, std::make_index_sequence<Count>{});
    }

    template <size_t Count>
    consteval std::array<std::string_view, Count> repeated_capabilities(std::string_view capability) noexcept
    {
        std::array<std::string_view, Count> values{};
        for (size_t i = 0; i < Count; ++i)
        {
            values[i] = capability;
        }
        return values;
    }

    // ---- provides（扁平能力声明） ----
    consteval Provision provision(std::string_view capability, uint32_t api_revision = 1,
                                  int16_t slot = kNoSlot) noexcept
    {
        Provision value{};
        value.capability   = capability;
        value.api_revision = api_revision;
        value.slot         = slot;
        return value;
    }

    consteval std::array<Provision, 1> provides1(std::string_view capability, uint32_t api_revision = 1,
                                                 int16_t slot = kNoSlot) noexcept
    {
        return { provision(capability, api_revision, slot) };
    }

    consteval std::array<Provision, 2> provides2(std::string_view first, uint32_t first_revision,
                                                 int16_t first_slot, std::string_view second,
                                                 uint32_t second_revision, int16_t second_slot) noexcept
    {
        return { provision(first, first_revision, first_slot), provision(second, second_revision, second_slot) };
    }

    consteval std::array<Provision, 5> provides5(std::string_view capability, int16_t first_slot) noexcept
    {
        // 同一插件的 5 条能力：名字前缀相同但后缀不同、修订不同、占两个槽位，其余无槽位
        std::array<Provision, 5> value{};
        value[0] = provision(capability, 1, first_slot);
        value[1] = provision("ID.Case.CapA.P1", 2, static_cast<int16_t>(first_slot + 1));
        value[2] = provision("ID.Case.CapA.P2", 3, kNoSlot);
        value[3] = provision("ID.Case.CapA.P3", 4, kNoSlot);
        value[4] = provision("ID.Case.CapA.P4", 5, kNoSlot);
        return value;
    }

    /**
     *  @brief 构造一个 catalog 条目：声明 + 完整元数据
     *  @tparam ProvidesCapacity 该插件 provides 容量 P（不是全局固定 4）
     *  @param groups 依赖的二维名字清单（外层 AND、内层 OR）
     *  @param capabilities 每个需求组的能力名，长度必须等于组数
     *  @param provides 扁平能力声明；写满 ProvidesCapacity 条
     *  @note 逐候选修订默认统一为 [min_revision, max_revision]；需要逐候选不同时由用例在
     *        返回对象上直接改 metadata.revisions。
     */
    template <size_t ProvidesCapacity = 1, typename Identity, typename... Groups>
    consteval auto make_entry(Identity identity, const std::tuple<Groups...>& groups,
                              const std::array<std::string_view, sizeof...(Groups)>& capabilities,
                              std::string_view target, std::string_view cpp_namespace,
                              const std::array<Provision, ProvidesCapacity>& provides,
                              PluginKind kind = PluginKind::Module,
                              PluginLifecycle lifecycle = PluginLifecycle::Standard,
                              uint32_t version = 1,
                              uint32_t min_revision = 1,
                              uint32_t max_revision = 1) noexcept
        -> PluginCatalogEntry<sizeof...(Groups),
                              (ID::PluginCompileTime::detail::group_size<Groups>::value + ... + 0),
                              ProvidesCapacity>
    {
        constexpr size_t kGroups     = sizeof...(Groups);
        constexpr size_t kCandidates = (ID::PluginCompileTime::detail::group_size<Groups>::value + ... + 0);

        PluginCatalogEntry<kGroups, kCandidates, ProvidesCapacity> entry{};
        entry.declaration = ID::PluginManager::declare_dependencies(identity, groups);

        PluginMetadata<kGroups, kCandidates, ProvidesCapacity>& metadata = entry.metadata;
        metadata.target         = target;
        metadata.cpp_namespace  = cpp_namespace;
        metadata.kind           = kind;
        metadata.lifecycle      = lifecycle;
        metadata.version        = version;
        metadata.contract_count = static_cast<uint16_t>(kGroups);
        metadata.provides_count = static_cast<uint16_t>(ProvidesCapacity);
        for (size_t provide = 0; provide < ProvidesCapacity; ++provide)
        {
            metadata.provides[provide] = provides[provide];
        }

        // 注意：这里必须用 const 而不是 constexpr。GCC 16.2 在 constexpr 局部变量的
        // 初始化器里对"由 consteval 调用方传入的 tuple 引用"做嵌套读取时，会误报
        // "'groups' is not a constant expression"；改用 const 后走外层 consteval 求值即可。
        const auto kFlatNames = detail::flatten_group_names(groups);
        size_t         revision_cursor = 0;
        for (size_t group = 0; group < kGroups; ++group)
        {
            const uint16_t begin = entry.declaration.group_begin[group];
            const uint16_t end   = entry.declaration.group_begin[group + 1];
            const uint16_t count = static_cast<uint16_t>(end - begin);
            metadata.contracts[group].capability     = capabilities[group];
            metadata.contracts[group].revision_begin = static_cast<uint16_t>(revision_cursor);
            metadata.contracts[group].revision_count = count;
            for (uint16_t candidate = 0; candidate < count; ++candidate)
            {
                metadata.revisions[revision_cursor + candidate] =
                    CandidateRevision{ kFlatNames[revision_cursor + candidate], min_revision, max_revision };
            }
            revision_cursor += count;
        }
        return entry;
    }

    /**
     *  @brief 用已有声明（tuple 或矩形重载的返回值）构造 catalog 条目
     *  @note 元数据契约按组的候选名逐项生成，名字 view 指向静态存储；用于矩形重载等不便走
     *        make_entry 的场景。
     */
    template <size_t ProvidesCapacity = 1, typename Declaration>
    consteval auto entry_from_declaration(Declaration declaration,
                                          const std::array<std::string_view, Declaration::kGroupCapacity>& capabilities,
                                          const std::array<std::string_view, Declaration::kCandidateCapacity>& revision_names,
                                          std::string_view target, std::string_view cpp_namespace,
                                          const std::array<Provision, ProvidesCapacity>& provides,
                                          PluginKind kind = PluginKind::Module,
                                          PluginLifecycle lifecycle = PluginLifecycle::Standard,
                                          uint32_t version = 1, uint32_t min_revision = 1,
                                          uint32_t max_revision = 1) noexcept
        -> PluginCatalogEntry<Declaration::kGroupCapacity, Declaration::kCandidateCapacity, ProvidesCapacity>
    {
        constexpr size_t kGroups     = Declaration::kGroupCapacity;
        constexpr size_t kCandidates = Declaration::kCandidateCapacity;

        PluginCatalogEntry<kGroups, kCandidates, ProvidesCapacity> entry{};
        entry.declaration = declaration;
        entry.metadata.target         = target;
        entry.metadata.cpp_namespace  = cpp_namespace;
        entry.metadata.kind           = kind;
        entry.metadata.lifecycle      = lifecycle;
        entry.metadata.version        = version;
        entry.metadata.contract_count = static_cast<uint16_t>(kGroups);
        entry.metadata.provides_count = static_cast<uint16_t>(ProvidesCapacity);
        for (size_t provide = 0; provide < ProvidesCapacity; ++provide)
        {
            entry.metadata.provides[provide] = provides[provide];
        }
        for (size_t group = 0; group < kGroups; ++group)
        {
            const uint16_t begin = declaration.group_begin[group];
            const uint16_t end   = declaration.group_begin[group + 1];
            const uint16_t count = static_cast<uint16_t>(end - begin);
            entry.metadata.contracts[group].capability     = capabilities[group];
            entry.metadata.contracts[group].revision_begin = static_cast<uint16_t>(begin);
            entry.metadata.contracts[group].revision_count = count;
            for (uint16_t candidate = 0; candidate < count; ++candidate)
            {
                entry.metadata.revisions[begin + candidate] =
                    CandidateRevision{ revision_names[begin + candidate], min_revision, max_revision };
            }
        }
        return entry;
    }

    // ---- 固定插件的 target / namespace / capability / slot 映射（与 kName* 一一对应） ----
    constexpr std::string_view target_of(std::string_view name) noexcept
    {
        if (name == kNameAlpha)   { return kTargetAlpha; }
        if (name == kNameBeta)    { return kTargetBeta; }
        if (name == kNameGamma)   { return kTargetGamma; }
        if (name == kNameDelta)   { return kTargetDelta; }
        if (name == kNameEpsilon) { return kTargetEpsilon; }
        if (name == kNameZeta)    { return kTargetZeta; }
        if (name == kNameEta)     { return kTargetEta; }
        if (name == kNameTheta)   { return kTargetTheta; }
        return {};
    }

    constexpr std::string_view namespace_of(std::string_view name) noexcept
    {
        if (name == kNameAlpha)   { return kNamespaceAlpha; }
        if (name == kNameBeta)    { return kNamespaceBeta; }
        if (name == kNameGamma)   { return kNamespaceGamma; }
        if (name == kNameDelta)   { return kNamespaceDelta; }
        if (name == kNameEpsilon) { return kNamespaceEpsilon; }
        if (name == kNameZeta)    { return kNamespaceZeta; }
        if (name == kNameEta)     { return kNamespaceEta; }
        if (name == kNameTheta)   { return kNamespaceTheta; }
        return {};
    }

    constexpr std::string_view capability_of(std::string_view name) noexcept
    {
        if (name == kNameAlpha)   { return kCapAlpha; }
        if (name == kNameBeta)    { return kCapBeta; }
        if (name == kNameGamma)   { return kCapGamma; }
        if (name == kNameDelta)   { return kCapDelta; }
        if (name == kNameEpsilon) { return kCapEpsilon; }
        if (name == kNameZeta)    { return kCapZeta; }
        if (name == kNameEta)     { return kCapEta; }
        if (name == kNameTheta)   { return kCapTheta; }
        return {};
    }

    constexpr int16_t slot_of(std::string_view name) noexcept
    {
        if (name == kNameAlpha)   { return 0; }
        if (name == kNameBeta)    { return 1; }
        if (name == kNameGamma)   { return 2; }
        if (name == kNameDelta)   { return 3; }
        if (name == kNameEpsilon) { return 4; }
        if (name == kNameZeta)    { return 5; }
        if (name == kNameEta)     { return 6; }
        if (name == kNameTheta)   { return 7; }
        return kNoSlot;
    }

    /**
     *  @brief 无依赖叶插件：提供自己的固定能力、固定槽位
     */
    consteval auto entry_leaf(PluginIdentity identity) noexcept -> PluginCatalogEntry<0, 0, 1>
    {
        return make_entry(identity, std::tuple<>{}, std::array<std::string_view, 0>{},
                          target_of(identity.view()), namespace_of(identity.view()),
                          provides1(capability_of(identity.view()), 1, slot_of(identity.view())));
    }

    /**
     *  @brief 单需求单候选插件：需求能力取自候选，默认修订区间 1..1
     */
    consteval auto entry_dep(PluginIdentity identity, std::string_view candidate,
                             uint32_t provider_revision = 1, uint32_t min_revision = 1,
                             uint32_t max_revision = 1) noexcept -> PluginCatalogEntry<1, 1, 1>
    {
        return make_entry(identity, std::tuple{ group1(candidate) },
                          std::array<std::string_view, 1>{ capability_of(candidate) },
                          target_of(identity.view()), namespace_of(identity.view()),
                          provides1(capability_of(identity.view()), provider_revision, slot_of(identity.view())),
                          PluginKind::Module, PluginLifecycle::Standard, 1, min_revision, max_revision);
    }

    // ---- 生成式名字池：容量边界用例（129/257 等）与 16 候选组使用 ----
    inline constexpr size_t kGeneratedCapacity = 300;

    struct TextBuffer
    {
        char     data[kMaxNameBytes + 1] = {};
        uint32_t length                  = 0;

        constexpr std::string_view view() const noexcept
        {
            return { data, length };
        }
    };

    struct TextPool
    {
        TextBuffer items[kGeneratedCapacity] = {};
    };

    consteval uint32_t write_decimal(char* out, size_t number) noexcept
    {
        // 编号不会超过 4 位；正序写入，返回字节数
        char   digits[8] = {};
        size_t count     = 0;
        if (number == 0)
        {
            out[0] = '0';
            return 1;
        }
        while (number > 0)
        {
            digits[count++] = static_cast<char>('0' + (number % 10));
            number /= 10;
        }
        for (size_t i = 0; i < count; ++i)
        {
            out[i] = digits[count - 1 - i];
        }
        return static_cast<uint32_t>(count);
    }

    consteval TextPool make_text_pool(std::string_view prefix) noexcept
    {
        TextPool pool{};
        for (size_t i = 0; i < kGeneratedCapacity; ++i)
        {
            size_t cursor = 0;
            for (char character : prefix)
            {
                pool.items[i].data[cursor++] = character;
            }
            cursor += write_decimal(pool.items[i].data + cursor, i);
            pool.items[i].length = static_cast<uint32_t>(cursor);
        }
        return pool;
    }

    inline constexpr TextPool kGeneratedNamePool       = make_text_pool("ID.Case.N");
    inline constexpr TextPool kGeneratedTargetPool     = make_text_pool("IDCaseN");
    inline constexpr TextPool kGeneratedNamespacePool  = make_text_pool("ID::Case::N");
    inline constexpr TextPool kGeneratedCapabilityPool = make_text_pool("ID.Case.G");

    consteval PluginIdentity generated_identity(size_t index) noexcept
    {
        const TextBuffer& name = kGeneratedNamePool.items[index];
        return ID::PluginCompileTime::detail::make_identity(name.data, name.length);
    }

    constexpr std::string_view generated_name(size_t index) noexcept
    {
        return kGeneratedNamePool.items[index].view();
    }

    constexpr std::string_view generated_target(size_t index) noexcept
    {
        return kGeneratedTargetPool.items[index].view();
    }

    constexpr std::string_view generated_namespace(size_t index) noexcept
    {
        return kGeneratedNamespacePool.items[index].view();
    }

    constexpr std::string_view generated_capability(size_t index) noexcept
    {
        return kGeneratedCapabilityPool.items[index].view();
    }

    consteval auto generated_leaf(size_t index, std::string_view capability,
                                  int16_t slot = kNoSlot) noexcept -> PluginCatalogEntry<0, 0, 1>
    {
        return make_entry(generated_identity(index), std::tuple<>{}, std::array<std::string_view, 0>{},
                          generated_target(index), generated_namespace(index), provides1(capability, 1, slot));
    }

    template <size_t Count, size_t... Index>
    consteval auto generated_leaf_tuple(std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ generated_leaf(Index, generated_capability(Index), kNoSlot)... };
    }

    template <size_t Count>
    consteval auto generated_leaf_tuple() noexcept
    {
        return generated_leaf_tuple<Count>(std::make_index_sequence<Count>{});
    }

    template <size_t Count, size_t... Index>
    consteval auto generated_cap_leaf_tuple(std::string_view capability, std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ generated_leaf(Index, capability, kNoSlot)... };
    }

    template <size_t Count>
    consteval auto generated_cap_leaf_tuple(std::string_view capability) noexcept
    {
        return generated_cap_leaf_tuple<Count>(capability, std::make_index_sequence<Count>{});
    }

    // 生成名字的 string_view 表与身份宏表：证据只能保存静态存储的 view
    template <typename Pool>
    consteval auto make_pool_views(const Pool& pool) noexcept
    {
        std::array<std::string_view, kGeneratedCapacity> views{};
        for (size_t i = 0; i < kGeneratedCapacity; ++i)
        {
            views[i] = pool.items[i].view();
        }
        return views;
    }

    inline constexpr auto kGeneratedPluginViews = make_pool_views(kGeneratedNamePool);
    inline constexpr auto kGeneratedCapViews    = make_pool_views(kGeneratedCapabilityPool);

    // ---- 常用合法基线：两个/三个固定插件；证据反向用例都从它们派生 ----
    consteval auto baseline_catalog() noexcept
    {
        return std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };
    }

    consteval auto baseline_selected() noexcept
    {
        return std::array<PluginIdentity, 2>{ alpha(), beta() };
    }

    consteval auto baseline3_catalog() noexcept
    {
        return std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta), entry_dep(gamma(), kNameBeta) };
    }

    consteval auto baseline3_selected() noexcept
    {
        return std::array<PluginIdentity, 3>{ alpha(), beta(), gamma() };
    }

    // ---- 输入形状别名：容量全部来自显式类型参数，便于逐用例对照契约 ----
    template <typename T>
    using Plain = std::remove_const_t<T>;

    template <typename Catalog, size_t ActiveCapacity, size_t SelectedCapacity, size_t BindingCapacity,
              size_t EdgeCapacity, size_t SlotCapacity = kMaxSlots>
    using ShapeOf = AssemblyShape<Catalog, ActiveCapacity, SelectedCapacity, BindingCapacity, EdgeCapacity, SlotCapacity>;

    template <typename Shape>
    consteval AssemblyInput<Shape> make_input(const typename Shape::CatalogTypeAlias& catalog,
                                              const std::array<PluginIdentity, Shape::kActiveCapacity>& selected) noexcept
    {
        AssemblyInput<Shape> input{};
        input.catalog  = catalog;
        input.selected = selected;
        return input;
    }

    // ---- 证据用的静态名字池：证据里的 string_view 必须指向静态存储，不能指向计划/输入的副本 ----
    inline constexpr std::array<std::string_view, 13> kPluginNamePool{
        kNameAlpha, kNameBeta, kNameGamma, kNameDelta, kNameEpsilon,
        kNameZeta, kNameEta, kNameTheta, kNameGhost,
        "ID.Case.Alpha.Alt", "ID.Case.Beta.Alt", "ID.Case.Gamma.Alt", "ID.Case.Delta.Alt" };

    inline constexpr std::array<std::string_view, 12> kCapabilityNamePool{
        kCapAlpha, kCapBeta, kCapGamma, kCapDelta, kCapEpsilon,
        kCapZeta, kCapEta, kCapTheta,
        "ID.Case.CapA.P1", "ID.Case.CapA.P2", "ID.Case.CapA.P3", "ID.Case.CapA.P4" };

    /**
     *  @brief 在静态名字池里找同名 view
     *  @return 池内 view（指向字面量，静态存储）；找不到返回空 view
     *  @note 证据只允许保存静态存储的 view；直接保存计划里的 view 会在计划对象复制后悬空。
     */
    template <typename Pool>
    constexpr std::string_view static_name(const Pool& pool, std::string_view text) noexcept
    {
        for (const std::string_view candidate : pool)
        {
            if (candidate == text)
            {
                return candidate;
            }
        }
        return {};
    }

    // ---- matched 计划表读取（schema2：每需求是一个命中集合，schema1 语义下恒为 1 条） ----
    /**
     *  @brief (canonical active, 需求组) -> matched 组槽位下标
     *  @param active canonical active 下标
     *  @param group  需求组下标（外层 AND 的第几组）
     *  @return `active * kMaxRequirements + group`
     *  @note 组槽位编址是生产头的公共契约（kGroupSlots = kMaxActive × kMaxRequirements）；
     *        本函数只做下标换算，不复制任何匹配/诊断逻辑。
     */
    constexpr size_t group_slot_of(size_t active, size_t group) noexcept
    {
        return active * kMaxRequirements + group;
    }

    /**
     *  @brief 某需求组的命中条数（0 / 1 / N）
     *  @return 该组槽位的区间长度 `match_begin[slot + 1] - match_begin[slot]`
     *  @note 未使用的组槽位返回 0；schema1 的合法计划恒为 1（0 与 >=2 都不是合法计划）。
     */
    template <typename Plan>
    constexpr size_t matched_count_of(const Plan& plan, size_t active, size_t group) noexcept
    {
        return static_cast<size_t>(plan.matched_count(group_slot_of(active, group)));
    }

    /**
     *  @brief 某需求组第 ordinal 条命中对应的 canonical active 下标
     *  @param ordinal 组内序号（0 起），必须小于 matched_count_of(...)
     *  @note 组内按 canonical active 下标升序排列，因此序号与候选书写顺序无关。
     */
    template <typename Plan>
    constexpr uint16_t matched_active_at(const Plan& plan, size_t active, size_t group, size_t ordinal) noexcept
    {
        const size_t slot = group_slot_of(active, group);
        return plan.match_active[static_cast<size_t>(plan.match_begin[slot]) + ordinal];
    }

    /**
     *  @brief 某需求组"恰好命中 expected 一个 active"
     *  @return 命中条数为 1 且唯一命中项等于 expected；0 条或 >=2 条都判 false
     */
    template <typename Plan>
    constexpr bool matched_exactly(const Plan& plan, size_t active, size_t group, uint16_t expected) noexcept
    {
        return matched_count_of(plan, active, group) == 1 && matched_active_at(plan, active, group, 0) == expected;
    }

    // ---- 身份宏拥有存储：证据里的 macro 是 string_view，必须指向静态存储 ----
    struct MacroName
    {
        char     data[kMacroCapacity] = {};
        uint32_t length               = 0;

        constexpr std::string_view view() const noexcept
        {
            return { data, length };
        }
    };

    /**
     *  @brief 按构建原语同一口径把 target 归一化成身份宏（独立实现，用于交叉验证生产实现）
     */
    consteval MacroName make_macro_name(std::string_view target) noexcept
    {
        constexpr char kPrefix[] = "ID_USE_PLUGIN_";
        MacroName      name{};
        size_t         cursor = 0;
        for (char character : kPrefix)
        {
            if (character == '\0')
            {
                break;
            }
            name.data[cursor++] = character;
        }
        for (char character : target)
        {
            if (character >= 'a' && character <= 'z')
            {
                character = static_cast<char>(character - 'a' + 'A');
            }
            else if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                       character == '_'))
            {
                character = '_';
            }
            name.data[cursor++] = character;
        }
        name.length = static_cast<uint32_t>(cursor);
        return name;
    }

    struct MacroEntry
    {
        std::string_view target{};
        MacroName        macro{};
    };

    template <size_t Count>
    consteval std::array<MacroEntry, Count> make_macro_table(const std::array<std::string_view, Count>& targets) noexcept
    {
        std::array<MacroEntry, Count> table{};
        for (size_t i = 0; i < Count; ++i)
        {
            table[i].target = targets[i];
            table[i].macro  = make_macro_name(targets[i]);
        }
        return table;
    }

    inline constexpr auto kMacroTable = make_macro_table(std::array<std::string_view, 8>{
        kTargetAlpha, kTargetBeta, kTargetGamma, kTargetDelta,
        kTargetEpsilon, kTargetZeta, kTargetEta, kTargetTheta });

    template <typename Table>
    constexpr std::string_view macro_of(const Table& table, std::string_view target) noexcept
    {
        for (const MacroEntry& entry : table)
        {
            if (entry.target == target)
            {
                return entry.macro.view();
            }
        }
        return {};
    }

    // ---- 从 catalog tuple 取目标名表：证据需要按 catalog 下标取 target ----
    template <typename Catalog, size_t... Index>
    constexpr std::array<std::string_view, sizeof...(Index)> catalog_targets(const Catalog& catalog,
                                                                             std::index_sequence<Index...>) noexcept
    {
        return { std::get<Index>(catalog).metadata.target... };
    }

    /**
     *  @brief 由一次合法计划派生证据（只用于小规模合法对照；证据派生输入是同一份声明数据）
     *  @note 生产上证据应由 CMake 从同一清单派生；P1 没有 JSON 组装链，因此不声称该闭环已建立。
     */
    template <typename Shape, typename NamePool, typename CapabilityPool, typename MacroTable>
    constexpr AssemblyEvidence<Shape> evidence_from_plan(const AssemblyResult<Shape>&   plan,
                                                         const typename Shape::CatalogTypeAlias& catalog,
                                                         const NamePool& names, const CapabilityPool& capabilities,
                                                         const MacroTable& macros) noexcept
    {
        AssemblyEvidence<Shape> evidence{};
        const auto              targets =
            catalog_targets(catalog, std::make_index_sequence<std::tuple_size_v<typename Shape::CatalogTypeAlias>>{});

        evidence.selected_count = plan.active_count;
        for (uint16_t active = 0; active < plan.active_count; ++active)
        {
            evidence.selected[active].name =
                static_name(names, plan.active_identity[active].view());
            evidence.selected[active].target    = targets[plan.active_catalog_index[active]];
            evidence.selected[active].macro     = macro_of(macros, evidence.selected[active].target);
            evidence.selected[active].plugin_id = plan.active_identity[active].id;
        }

        evidence.binding_count = 0;
        for (uint16_t active = 0; active < plan.active_count; ++active)
        {
            for (uint16_t group = 0; group < plan.active_requirement_count[active]; ++group)
            {
                // schema2：每需求的命中由 match_begin / match_active 描述成一个集合（0 / 1 / N）。
                // 证据记录的是逐需求 provider，因此取组内首条命中；合法计划按 schema1 语义恒为 1 条。
                // 0 条命中时填空 view，让证据校验按具体类别拒绝，而不是静默取到无关 active。
                const size_t   hit_count = matched_count_of(plan, active, group);
                const uint16_t provider  = hit_count > 0 ? matched_active_at(plan, active, group, 0) : kNoIndex;
                evidence.bindings[evidence.binding_count].dependent =
                    static_name(names, plan.active_identity[active].view());
                evidence.bindings[evidence.binding_count].provider =
                    provider == kNoIndex ? std::string_view{} : static_name(names, plan.active_identity[provider].view());
                evidence.bindings[evidence.binding_count].requirement = group;
                ++evidence.binding_count;
            }
        }

        evidence.edge_count = plan.edge_count;
        for (uint16_t dependent = 0; dependent < plan.active_count; ++dependent)
        {
            for (uint16_t edge = plan.edge_begin[dependent]; edge < plan.edge_begin[dependent + 1]; ++edge)
            {
                evidence.edges[edge].from = static_name(names, plan.active_identity[plan.edge_target[edge]].view());
                evidence.edges[edge].to   = static_name(names, plan.active_identity[dependent].view());
            }
        }

        evidence.slot_count = 0;
        for (uint16_t index = 0; index < plan.provision_count; ++index)
        {
            if (plan.provision_slot[index] == kNoSlot)
            {
                continue;
            }
            evidence.slots[evidence.slot_count].owner = static_name(names, plan.active_identity[plan.provision_owner[index]].view());
            evidence.slots[evidence.slot_count].capability =
                static_name(capabilities, plan.provision_capability_view(index));
            evidence.slots[evidence.slot_count].slot = plan.provision_slot[index];
            ++evidence.slot_count;
        }

        evidence.runtime_count = plan.provision_count;
        for (uint16_t index = 0; index < plan.provision_count; ++index)
        {
            const uint16_t owner = plan.provision_owner[index];
            evidence.runtime[index].name         = static_name(names, plan.active_identity[owner].view());
            evidence.runtime[index].capability   = static_name(capabilities, plan.provision_capability_view(index));
            evidence.runtime[index].plugin_id    = plan.active_identity[owner].id;
            evidence.runtime[index].api_revision = plan.provision_api_revision[index];
        }
        return evidence;
    }

    /**
     *  @brief 先求一次无证据计划，再按该计划填证据，返回可再次校验的输入
     *  @note 仅用于合法对照与证据反向用例的基线；不是生产构建链。
     */
    template <typename Shape>
    consteval AssemblyInput<Shape> with_evidence(AssemblyInput<Shape> input) noexcept
    {
        const AssemblyResult<Shape> draft = ID::PluginManager::sort_and_validate(input);
        input.evidence =
            evidence_from_plan<Shape>(draft, input.catalog, kPluginNamePool, kCapabilityNamePool, kMacroTable);
        return input;
    }

    /**
     *  @brief 单对象构造带证据输入：catalog/selected 直接写进唯一的 AssemblyInput，再就地填证据
     *  @param catalog  完整 catalog（拥有声明与元数据）
     *  @param selected 选中身份表
     *  @return 可交给 sort_and_validate 的输入；返回路径靠 NRVO，不再复制整份定长输入
     *  @note 生产 API 只提供 consteval 入口，输入按值持有 catalog 与证据数组是公共契约；
     *        这里省掉的是测试夹具自己的"make_input 临时对象 -> with_evidence 形参 -> 返回值"
     *        两次整份复制，不改变 input 的成员形状、不减少任何证据记录、不跳过校验。
     *        视图仍指向静态存储的名字池，因此计划/结果复制后不会悬空。
     */
    template <typename Shape>
    consteval AssemblyInput<Shape> make_evidence_input(
        const typename Shape::CatalogTypeAlias& catalog,
        const std::array<PluginIdentity, Shape::kActiveCapacity>& selected) noexcept
    {
        AssemblyInput<Shape> input{};
        input.catalog  = catalog;
        input.selected = selected;
        const AssemblyResult<Shape> draft = ID::PluginManager::sort_and_validate(input);
        input.evidence =
            evidence_from_plan<Shape>(draft, input.catalog, kPluginNamePool, kCapabilityNamePool, kMacroTable);
        return input;
    }

    inline constexpr auto kGeneratedMacroTable = make_macro_table(make_pool_views(kGeneratedTargetPool));

    template <typename Shape>
    consteval AssemblyInput<Shape> with_generated_evidence(AssemblyInput<Shape> input) noexcept
    {
        const AssemblyResult<Shape> draft = ID::PluginManager::sort_and_validate(input);
        input.evidence = evidence_from_plan<Shape>(draft, input.catalog, kGeneratedPluginViews, kGeneratedCapViews,
                                                   kGeneratedMacroTable);
        return input;
    }

    constexpr size_t generated_index_of(std::string_view name) noexcept
    {
        for (size_t i = 0; i < kGeneratedCapacity; ++i)
        {
            if (kGeneratedPluginViews[i] == name)
            {
                return i;
            }
        }
        return kGeneratedCapacity;
    }

    /**
     *  @brief 只含"单能力、无槽位、无依赖叶插件"的候选/选中集合的直填证据
     *  @note 用于 128/129、256/257 这类容量边界用例，避免"先排序一次拿 draft 再排序一次"把
     *        常量求值预算翻倍；叶插件的身份/修订/目标都能从生成池直接读出，不依赖计划结果。
     */
    template <typename Shape>
    constexpr AssemblyEvidence<Shape> generated_leaf_evidence(
        const std::array<PluginIdentity, Shape::kActiveCapacity>& selected) noexcept
    {
        AssemblyEvidence<Shape> evidence{};
        evidence.selected_count = Shape::kActiveCapacity;
        evidence.runtime_count  = Shape::kActiveCapacity;
        for (size_t i = 0; i < Shape::kActiveCapacity; ++i)
        {
            const size_t index = generated_index_of(selected[i].view());
            if (index >= kGeneratedCapacity) [[unlikely]]
            {
                continue;
            }
            const auto target = kGeneratedTargetPool.items[index].view();
            evidence.selected[i].name      = kGeneratedPluginViews[index];
            evidence.selected[i].target    = target;
            evidence.selected[i].macro     = macro_of(kGeneratedMacroTable, target);
            evidence.selected[i].plugin_id = selected[i].id;
            evidence.runtime[i].name         = kGeneratedPluginViews[index];
            evidence.runtime[i].capability   = kGeneratedCapViews[index];
            evidence.runtime[i].plugin_id    = selected[i].id;
            evidence.runtime[i].api_revision = 1;
        }
        return evidence;
    }

    template <typename Shape>
    consteval AssemblyInput<Shape> with_generated_leaf_evidence(AssemblyInput<Shape> input) noexcept
    {
        input.evidence = generated_leaf_evidence<Shape>(input.selected);
        return input;
    }

    // ---- 结果字段读取 ----
    template <typename Plan>
    consteval PluginError error_of(const Plan& plan) noexcept
    {
        return plan.diagnostic.code;
    }

    template <PluginError Expected, typename Plan>
    consteval bool error_matches(const Plan& plan) noexcept
    {
        return error_of(plan) == Expected;
    }

    template <PluginError Expected, typename Plan>
    consteval bool error_differs(const Plan& plan) noexcept
    {
        return error_of(plan) != Expected;
    }

    /**
     *  @brief 校验 error_nodes 是真实闭合环路径（成员不重复、相邻边真实存在、末尾闭回起点）
     */
    template <typename Plan>
    consteval bool cycle_path_is_closed(const Plan& plan) noexcept
    {
        if (!plan.error_is_cycle || plan.error_node_count < 2 || plan.error_node_count > plan.active_count)
        {
            return false;
        }
        for (uint16_t i = 0; i < plan.error_node_count; ++i)
        {
            const uint16_t dependent = plan.error_nodes[i];
            const uint16_t upstream  = plan.error_nodes[(i + 1) % plan.error_node_count];
            if (dependent >= plan.active_count || upstream >= plan.active_count)
            {
                return false;
            }
            bool has_edge = false;
            for (uint16_t edge = plan.edge_begin[dependent]; edge < plan.edge_begin[dependent + 1]; ++edge)
            {
                if (plan.edge_target[edge] == upstream)
                {
                    has_edge = true;
                    break;
                }
            }
            if (!has_edge)
            {
                return false;
            }
            for (uint16_t j = static_cast<uint16_t>(i + 1); j < plan.error_node_count; ++j)
            {
                if (plan.error_nodes[j] == dependent)
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace ID::Bench::Production

/**
 *  @brief 反向用例的类别契约：实际错误类别必须严格等于 code
 *  @param plan 计划表达式（可含逗号，因此用可变参数）
 *  @param code 期望的错误类别（PluginError 枚举名）
 */
#define ID_PRODUCTION_EXPECT_ERROR(plan, code)                                                                       \
    static_assert(::ID::Bench::Production::error_matches<::ID::PluginCompileTime::PluginError::code>(plan),          \
                  "ID_ASSEMBLY_ERROR_MISMATCH:" #code);                                                              \
    static_assert(::ID::Bench::Production::error_differs<::ID::PluginCompileTime::PluginError::code>(plan),          \
                  "ID_ASSEMBLY_ERROR_EXPECTED:" #code)
