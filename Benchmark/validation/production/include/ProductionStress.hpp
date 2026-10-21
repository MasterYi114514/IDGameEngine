#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

#include "ProductionCaseSupport.hpp"

/**
 *  P1 生产 API 满规模压力输入（Benchmark/validation/production，不是生产代码）
 *  1. 只使用生产 API 的完整元数据：PluginCatalogEntry<32, 512, 1>（32 需求组、每组 16 候选、
 *     每条目一条 provides）与 AssemblyInput<Shape>；
 *  2. 拓扑有意保持“所有下游都依赖 P0”的浅图：生产 API 要求候选自己声明提供需求能力，
 *     在 256 catalog / 128 active 下无法同时满足“多未选备选同能力”和深链；深链由 32x32x1 档覆盖。
 *  3. 证据由已知构造确定性直填，不做“先排序一次拿 draft 再排序一次”，避免把常量求值预算翻倍；
 *     这不是 CMake 派生证据链，P1 没有 JSON 组装，不能声称该闭环已建立。
 */

namespace ID::Bench::Production::Stress
{
    struct StressProfile
    {
        size_t active       = 128;
        size_t catalog      = 256;
        size_t requirements = 32;
        size_t candidates   = 16;
    };

    consteval size_t spare_index(const StressProfile& profile, size_t dependent, size_t group, size_t candidate) noexcept
    {
        const size_t spare_count = profile.catalog - profile.active;
        const size_t ordinal     = (dependent * profile.requirements + group) * (profile.candidates - 1) + (candidate - 1);
        return profile.active + ordinal % spare_count;
    }

    inline constexpr std::string_view kCap0 = generated_capability(0);

    template <size_t Groups, size_t Candidates, size_t... Index>
    consteval auto make_group_tuple(const std::array<std::array<std::string_view, Candidates>, Groups>& groups,
                                    std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ groups[Index]... };
    }

    template <const StressProfile& Profile, size_t Index>
    consteval auto make_catalog_entry() noexcept
    {
        if constexpr (Index == 0)
        {
            // P0：无需求，提供 Cap0 并占槽 0；所有下游都绑定它
            return generated_leaf(0, generated_capability(0), 0);
        }
        else if constexpr (Index < Profile.active)
        {
            constexpr size_t kGroups = Profile.requirements;
            std::array<std::array<std::string_view, Profile.candidates>, kGroups> groups{};
            for (size_t group = 0; group < kGroups; ++group)
            {
                groups[group][0] = generated_name(0);
                for (size_t candidate = 1; candidate < Profile.candidates; ++candidate)
                {
                    groups[group][candidate] =
                        generated_name(spare_index(Profile, Index, group, candidate));
                }
            }
            const auto tuple_groups = make_group_tuple(groups, std::make_index_sequence<kGroups>{});
            const int16_t slot = Index < 64 ? static_cast<int16_t>(Index) : kNoSlot;
            return make_entry(generated_identity(Index), tuple_groups, repeated_capabilities<kGroups>(kCap0),
                              generated_target(Index), generated_namespace(Index),
                              provides1(generated_capability(Index), 1, slot));
        }
        else
        {
            // 未选中的备选后端：只提供 Cap0，不占槽位，用来把每组候选真正凑到 16
            return generated_leaf(Index, generated_capability(0), kNoSlot);
        }
    }

    template <const StressProfile& Profile, size_t... Index>
    consteval auto make_catalog(std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ make_catalog_entry<Profile, Index>()... };
    }

    template <const StressProfile& Profile, size_t... Index>
    consteval auto make_selected(std::index_sequence<Index...>) noexcept
    {
        return std::array<PluginIdentity, sizeof...(Index)>{ generated_identity(Index)... };
    }

    template <const StressProfile& Profile, typename Shape>
    constexpr AssemblyEvidence<Shape> make_evidence(
        const std::array<PluginIdentity, Shape::kActiveCapacity>& selected) noexcept
    {
        static_assert(Shape::kActiveCapacity == Profile.active, "压力证据：active 容量必须等于 profile");
        AssemblyEvidence<Shape> evidence{};
        evidence.selected_count = Profile.active;

        for (size_t i = 0; i < Profile.active; ++i)
        {
            const auto target = generated_target(i);
            evidence.selected[i].name      = generated_name(i);
            evidence.selected[i].target    = target;
            evidence.selected[i].macro     = macro_of(kGeneratedMacroTable, target);
            evidence.selected[i].plugin_id = selected[i].id;

            evidence.runtime[i].name         = generated_name(i);
            evidence.runtime[i].capability   = generated_capability(i);
            evidence.runtime[i].plugin_id    = selected[i].id;
            evidence.runtime[i].api_revision = 1;
        }
        evidence.runtime_count = Profile.active;

        size_t binding = 0;
        for (size_t dependent = 1; dependent < Profile.active; ++dependent)
        {
            for (size_t group = 0; group < Profile.requirements; ++group)
            {
                evidence.bindings[binding].dependent   = generated_name(dependent);
                evidence.bindings[binding].provider    = generated_name(0);
                evidence.bindings[binding].requirement = static_cast<uint16_t>(group);
                ++binding;
            }
        }
        evidence.binding_count = binding;

        evidence.edge_count = 0;
        for (size_t dependent = 1; dependent < Profile.active; ++dependent)
        {
            evidence.edges[evidence.edge_count].from = generated_name(0);
            evidence.edges[evidence.edge_count].to   = generated_name(dependent);
            ++evidence.edge_count;
        }

        evidence.slot_count = 0;
        for (size_t i = 0; i < Profile.active && i < 64; ++i)
        {
            evidence.slots[evidence.slot_count].owner      = generated_name(i);
            evidence.slots[evidence.slot_count].capability = generated_capability(i);
            evidence.slots[evidence.slot_count].slot       = static_cast<int16_t>(i);
            ++evidence.slot_count;
        }
        return evidence;
    }

    template <const StressProfile& Profile, typename Shape>
    consteval AssemblyInput<Shape> with_evidence(AssemblyInput<Shape> input) noexcept
    {
        input.evidence = make_evidence<Profile, Shape>(input.selected);
        return input;
    }

    /**
     *  @brief 单对象构造满规模输入：catalog/selected/证据直接写进唯一的 AssemblyInput
     *  @param catalog  完整 catalog（拥有 256 条声明与元数据）
     *  @param selected 128 个选中身份
     *  @return 可交给 sort_and_validate 的输入；返回路径靠 NRVO
     *  @note 与 with_evidence(AssemblyInput) 的区别只是**构造方式**：不再经过
     *        "make_input 临时对象 -> 按值形参 -> 返回值" 的整份复制（满 16 候选时
     *        单份输入约 9 MB 定长数组），证据记录条数与内容完全一致，没有减少
     *        contracts/provides/candidate 规模，也没有跳过任何校验。
     */
    template <const StressProfile& Profile, typename Shape>
    consteval AssemblyInput<Shape> make_input_with_evidence(
        const typename Shape::CatalogTypeAlias& catalog,
        const std::array<PluginIdentity, Shape::kActiveCapacity>& selected) noexcept
    {
        static_assert(Shape::kActiveCapacity == Profile.active, "压力输入：active 容量必须等于 profile");
        AssemblyInput<Shape> input{};
        input.catalog  = catalog;
        input.selected = selected;
        input.evidence = make_evidence<Profile, Shape>(input.selected);
        return input;
    }

    /**
     *  @brief 核对压力证据的记录条数与 profile 一致（不减少任何记录，也不跳过校验）
     *  @return selected/绑定/边/槽位/运行期五类记录的条数都等于该 profile 的应有值
     *  @note 绑定 = (active-1) x requirements，边 = active-1（浅图，所有下游依赖 P0），
     *        槽位 = min(active, 64)，运行期 = active；候选记录数由 catalog 条目类型保证。
     */
    template <const StressProfile& Profile, typename Shape>
    constexpr bool evidence_complete(const AssemblyInput<Shape>& input) noexcept
    {
        constexpr size_t kExpectedSlots = Profile.active < 64 ? Profile.active : 64;
        return input.evidence.selected_count == Profile.active &&
               input.evidence.binding_count == (Profile.active - 1) * Profile.requirements &&
               input.evidence.edge_count == Profile.active - 1 && input.evidence.slot_count == kExpectedSlots &&
               input.evidence.runtime_count == Profile.active;
    }
} // namespace ID::Bench::Production::Stress
