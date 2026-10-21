#pragma once

#include <array>
#include <string_view>
#include <tuple>
#include <utility>

#include "FixturePluginApi.hpp"

/**
 *  压力输入生成器（只用于最大合法规模的 constexpr 成本测量与 128/129 active 边界）
 *  1. 名字来自静态存储的名字池："ID.Stress.P<编号>"，全部满足名字格式；
 *  2. 拓扑固定为"只依赖更小下标"，因此合法配置必然无环；
 *  3. 每个需求组的候选 = [1 个已选中上游] + [若干仅 catalog 的候选]，
 *     保证选中集合内每组恰好命中 1 个候选（唯一提供者），候选总量随参数放大。
 */

namespace ID::Bench::Fixture::Stress
{
    inline constexpr size_t kPoolSize = kMaxCatalog;

    struct NameBuffer
    {
        char     data[kNameCapacity] = {};
        uint32_t length              = 0;

        constexpr std::string_view view() const noexcept
        {
            return { data, length };
        }
    };

    consteval NameBuffer make_numbered_name(std::string_view prefix, size_t number) noexcept
    {
        NameBuffer buffer{};
        size_t     cursor = 0;
        for (char character : prefix)
        {
            buffer.data[cursor++] = character;
        }
        cursor += detail::write_decimal(buffer.data + cursor, number);
        buffer.length = static_cast<uint32_t>(cursor);
        return buffer;
    }

    struct NamePool
    {
        NameBuffer names[kPoolSize] = {};
    };

    consteval NamePool make_pool(std::string_view prefix) noexcept
    {
        NamePool pool{};
        for (size_t i = 0; i < kPoolSize; ++i)
        {
            pool.names[i] = make_numbered_name(prefix, i);
        }
        return pool;
    }

    // 名字池必须具有静态存储期：声明里的 string_view 在常量求值期指向它
    inline constexpr NamePool kPool = make_pool("ID.Stress.P");

    consteval PluginIdentity pool_identity(size_t index) noexcept
    {
        return detail::identity_from_chars(kPool.names[index].data, kPool.names[index].length);
    }

    /**
     *  @brief 压力配置：选中数 / catalog 数 / 每插件需求组上限 / 每组候选数
     *  @note 约束：catalog <= kPoolSize、catalog > active（候选数 > 1 时）、candidates >= 1
     */
    struct StressProfile
    {
        size_t active       = 0;
        size_t catalog      = 0;
        size_t requirements = 0;
        size_t candidates   = 0;
    };

    template <StressProfile Profile, size_t PluginIndex>
    consteval auto make_declaration() noexcept
    {
        // 只有被选中的插件声明需求；仅 catalog 的备用后端没有上游，也不会与自身同名
        constexpr size_t kGroupsBase = PluginIndex < Profile.active ? Profile.requirements : 0;
        constexpr size_t kGroups     = PluginIndex < kGroupsBase ? PluginIndex : kGroupsBase;
        constexpr size_t kCandidates = Profile.candidates;

        std::array<std::array<std::string_view, kCandidates>, kGroups> groups{};
        for (size_t group = 0; group < kGroups; ++group)
        {
            // 第 0 个候选是已选中的上游（下标更小，保证无环）
            groups[group][0] = kPool.names[PluginIndex - group - 1].view();
            for (size_t candidate = 1; candidate < kCandidates; ++candidate)
            {
                // 其余候选只在 catalog 中存在、不进入选中集合，用来放大候选总量
                const size_t spare_slot = (PluginIndex * Profile.requirements + group) * (kCandidates - 1) + (candidate - 1);
                const size_t spare      = Profile.active + spare_slot % (Profile.catalog - Profile.active);
                groups[group][candidate] = kPool.names[spare].view();
            }
        }
        return declare_dependencies(pool_identity(PluginIndex), groups);
    }

    template <StressProfile Profile, size_t... Index>
    consteval auto make_catalog(std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ make_declaration<Profile, Index>()... };
    }

    template <StressProfile Profile>
    consteval auto make_selected() noexcept
    {
        std::array<PluginIdentity, Profile.active> selected{};
        for (size_t i = 0; i < Profile.active; ++i)
        {
            selected[i] = pool_identity(i);
        }
        return selected;
    }
} // namespace ID::Bench::Fixture::Stress
