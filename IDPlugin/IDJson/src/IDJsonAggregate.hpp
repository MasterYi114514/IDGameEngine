#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "Arena/SegmentedArena.hpp"

#include "IDJsonValue.hpp"

/**
 *  数组与对象的追加助手（私有头，不进公开 include 路径）。
 *
 *  解析器与 JsonBuilder 都要向 arena 上的聚合值追加元素/成员，两边必须产出同一种布局：
 *  - 元素/成员连续存放；容量不足时换一块 2 倍大的连续块并遗弃旧块（arena 不回收单块，
 *    交给 JsonDocument::reset()/析构统一归还）；
 *  - 对象成员数超过 kObjectIndexThreshold 后建幂 2 开放寻址索引，装载因子保持 ≤ 0.5；
 *    重复键 last-wins（位置保持首次插入序）。
 *  这两条只在这里实现一次，两个调用方共用，避免出现第二份"各写一遍"的追加实现。
 *
 *  失败语义：一律返回 false，只可能是 arena 分配失败或 32 位计数溢出；由调用方映射到自己的错误码
 *  （解析器映射 JsonError::out_of_memory，构建器映射 JsonBuildError::out_of_memory）。
 *  失败时不改动已追加的内容，也不释放已经分配的块。
 */

namespace ID::Json::Detail
{
    // 聚合值的初始容量：小对象占多数，从 4 起步，按 2 倍扩容
    inline constexpr std::uint32_t kInitialAggregateCapacity = 4;

    // 对象索引的初始桶数（必须为 2 的幂）
    inline constexpr std::uint32_t kInitialBucketCapacity = 16;

    // 元素数/成员数与桶数都用 32 位计量：翻倍前的上限，超过即拒绝追加。
    // 没有这一条时 capacity * 2 会在 2^31 处回绕成 0，随后按 0 容量写入就越界了
    inline constexpr std::uint32_t kMaxAggregateCapacity = 0x80000000u;

    /**
     *  @brief 保证数组元素块还能再放一个元素（容量不足时换 2 倍大的新块）
     *  @param arena 元素块的来源
     *  @param ref 数组描述块
     *  @return 有可用槽位返回 true；分配失败或计数溢出返回 false
     */
    inline bool reserve_item_slot(Common::SegmentedArena& arena, ArrayRef& ref) noexcept
    {
        if (ref.count < ref.capacity)
        {
            return true;
        }
        if (ref.capacity >= kMaxAggregateCapacity) [[unlikely]]
        {
            return false;
        }

        const std::uint32_t new_capacity = ref.capacity == 0 ? kInitialAggregateCapacity : ref.capacity * 2;
        auto* items = arena.allocate_array<JsonValue>(new_capacity);
        if (items == nullptr) [[unlikely]]
        {
            return false;
        }
        if (ref.count != 0)
        {
            std::memcpy(items, ref.items, static_cast<std::size_t>(ref.count) * sizeof(JsonValue));
        }
        ref.items    = items;
        ref.capacity = new_capacity;
        return true;
    }

    /**
     *  @brief 追加一个数组元素
     *  @param arena 元素块的来源
     *  @param ref 数组描述块
     *  @param value 元素值（按值复制；字符串内容仍由 arena 持有）
     *  @return 成功返回 true；分配失败或计数溢出返回 false
     */
    inline bool append_item(Common::SegmentedArena& arena, ArrayRef& ref, const JsonValue& value) noexcept
    {
        if (!reserve_item_slot(arena, ref))
        {
            return false;
        }
        ref.items[ref.count] = value;
        ++ref.count;
        return true;
    }

    /**
     *  @brief 在对象成员里按字节查键
     *  @param ref 对象描述块
     *  @param key 键字符（可为 nullptr，仅当 key_length 为 0）
     *  @param key_length 键字节数
     *  @param out_index 命中时写入成员下标；未命中时不改动
     *  @return 命中返回 true
     *  @note 有索引时走哈希探测，没有索引时线性扫描（成员少时更快）
     */
    inline bool find_entry_index(const ObjectRef& ref, const char* const key, const std::uint32_t key_length,
                                 std::uint32_t& out_index) noexcept
    {
        if (ref.buckets != nullptr)
        {
            const std::string_view key_view =
                key_length == 0 ? std::string_view{} : std::string_view(key, key_length);
            const std::uint32_t mask = ref.bucket_mask;
            std::uint32_t       slot = fnv1a(key_view) & mask;
            for (;;)
            {
                const std::uint32_t bucket = ref.buckets[slot];
                if (bucket == 0)
                {
                    return false;
                }
                const ObjectEntry& entry = ref.entries[bucket - 1];
                if (entry.key_length == key_length &&
                    (key_length == 0 || std::memcmp(entry.key, key, key_length) == 0))
                {
                    out_index = bucket - 1;
                    return true;
                }
                slot = (slot + 1) & mask;
            }
        }

        for (std::uint32_t index = 0; index < ref.count; ++index)
        {
            const ObjectEntry& entry = ref.entries[index];
            if (entry.key_length == key_length &&
                (key_length == 0 || std::memcmp(entry.key, key, key_length) == 0))
            {
                out_index = index;
                return true;
            }
        }
        return false;
    }

    /**
     *  @brief 重建对象索引（旧桶数组遗弃给 arena 统一回收）
     *  @param arena 桶数组的来源
     *  @param ref 对象描述块
     *  @return 成功返回 true；分配失败或桶数溢出返回 false
     */
    inline bool rebuild_index(Common::SegmentedArena& arena, ObjectRef& ref) noexcept
    {
        std::uint32_t bucket_count = kInitialBucketCapacity;
        while (static_cast<std::uint64_t>(bucket_count) < static_cast<std::uint64_t>(ref.count) * 2)
        {
            if (bucket_count >= kMaxAggregateCapacity) [[unlikely]]
            {
                return false;
            }
            bucket_count <<= 1;
        }

        auto* buckets = arena.allocate_array<std::uint32_t>(bucket_count);
        if (buckets == nullptr) [[unlikely]]
        {
            return false;
        }
        std::memset(buckets, 0, static_cast<std::size_t>(bucket_count) * sizeof(std::uint32_t));

        const std::uint32_t mask = bucket_count - 1;
        for (std::uint32_t index = 0; index < ref.count; ++index)
        {
            const ObjectEntry& entry = ref.entries[index];
            const std::string_view key_view =
                entry.key_length == 0 ? std::string_view{} : std::string_view(entry.key, entry.key_length);
            std::uint32_t slot = fnv1a(key_view) & mask;
            while (buckets[slot] != 0)
            {
                slot = (slot + 1) & mask;
            }
            buckets[slot] = index + 1;
        }

        ref.buckets     = buckets;
        ref.bucket_mask = mask;
        return true;
    }

    /**
     *  @brief 追加一个对象成员
     *  @param arena 成员块与桶数组的来源
     *  @param ref 对象描述块
     *  @param key 键字符（已在 arena 或输入缓冲上，成对存指针 + 长度）
     *  @param key_length 键字节数
     *  @param value 成员值
     *  @return 成功返回 true；分配失败或计数溢出返回 false
     *  @note 重复键 last-wins：位置保持首次插入序，值取最后一次写入的；键指针也更新为最后一次的
     */
    inline bool append_entry(Common::SegmentedArena& arena, ObjectRef& ref, const char* const key,
                             const std::uint32_t key_length, const JsonValue& value) noexcept
    {
        std::uint32_t existing = 0;
        if (find_entry_index(ref, key, key_length, existing))
        {
            ref.entries[existing].value = value;
            return true;
        }

        if (ref.count == ref.capacity)
        {
            if (ref.capacity >= kMaxAggregateCapacity) [[unlikely]]
            {
                return false;
            }
            const std::uint32_t new_capacity =
                ref.capacity == 0 ? kInitialAggregateCapacity : ref.capacity * 2;
            auto* entries = arena.allocate_array<ObjectEntry>(new_capacity);
            if (entries == nullptr) [[unlikely]]
            {
                return false;
            }
            if (ref.count != 0)
            {
                std::memcpy(entries, ref.entries, static_cast<std::size_t>(ref.count) * sizeof(ObjectEntry));
            }
            ref.entries  = entries;
            ref.capacity = new_capacity;
        }

        ObjectEntry& entry = ref.entries[ref.count];
        entry.key          = key;
        entry.key_length   = key_length;
        entry.value        = value;
        ++ref.count;

        if (ref.count > kObjectIndexThreshold)
        {
            // 索引装载因子保持 ≤ 0.5：超过就重建（旧桶数组遗弃给 arena 统一回收）
            const bool need_rebuild =
                ref.buckets == nullptr ||
                static_cast<std::uint64_t>(ref.count) * 2 > static_cast<std::uint64_t>(ref.bucket_mask) + 1;
            if (need_rebuild)
            {
                return rebuild_index(arena, ref);
            }

            const std::string_view key_view =
                key_length == 0 ? std::string_view{} : std::string_view(key, key_length);
            std::uint32_t slot = fnv1a(key_view) & ref.bucket_mask;
            while (ref.buckets[slot] != 0)
            {
                slot = (slot + 1) & ref.bucket_mask;
            }
            ref.buckets[slot] = ref.count;      // 桶存"成员下标 + 1"，0 表示空桶
        }
        return true;
    }
} // namespace ID::Json::Detail
