#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "Core/Core.hpp"

#include "IDJsonCore.hpp"

/**
 *  JSON 值（DOM 节点）的紧凑表示。
 *
 *  设计取舍：
 *  - 每个值固定 16 字节。标量（null/bool/int/double）与 ≤ 7 字节的短字符串完全内联，不进 arena；
 *    只有长字符串、数组、对象才向所属 JsonDocument 的 arena 申请内存。读标量不分配、不追踪指针。
 *  - 长字符串的字符数据由 Detail::StringRef（{const char*, uint32}）描述：owning 解析时字符数据在
 *    arena 上，non_owning 解析时可能直接指向输入缓冲（零拷贝）。
 *  - 数组与对象用 arena 上的描述块（Detail::ArrayRef / Detail::ObjectRef）承载"指针 + 计数"，
 *    JsonValue 自身只存一个指针。这是把值压在 16 字节内的代价：读聚合值多一次解引用。
 *
 *  不变量：
 *  1. m_type 与实际存放的 m_payload 成员一一对应；类型不匹配时 as_* 返回缺省值、size() 返回 0、
 *     operator[] 与 begin()/end() 返回空区间，**不产生未定义行为**（调用方应先判断类型）。
 *  2. 从值取得的指针与 string_view 只在所属文档 reset()/析构之前有效；文档析构后一律悬垂。
 *  3. Detail:: 下的类型是实现布局，随实现变更；消费者只能经由本文件的访问器使用它们。
 *  4. 值本身不含所有权：它不释放任何内存，释放由所属 JsonDocument 负责。
 */

namespace ID::Json
{
    using std::size_t;
    using std::uint32_t;

    /**
     *  @brief 值的类型标签
     *  @note 字符串有两个取值：`InlineString`（≤ 7 字节，字符内联在 m_payload 内）与 `String`
     *        （字符数据在 arena 或输入缓冲上）。`is_string()` / `as_string()` 对两者都成立；
     *        直接比较 `type()` 时必须同时考虑这两个取值。
     *  @note 取值顺序与成员已冻结于本头；增删取值属于接口变更，必须升接口版本。
     */
    enum class JsonType : std::uint8_t
    {
        Null = 0,
        Bool,
        Int,
        Double,
        InlineString,
        String,
        Array,
        Object
    };

    // 短字符串内联容量：m_payload 的 8 字节中前 7 字节放字符，最后 1 字节放长度
    inline constexpr uint32_t kInlineStringCapacity = 7;

    namespace Detail
    {
        // 对象成员结构：JsonValue 的遍历访问器需要它的声明；完整定义在本文件后部
        // （它按值持有 JsonValue，因此只能定义在 JsonValue 之后）
        struct ObjectEntry;
    } // namespace Detail

    struct JsonValue
    {
        // alignas(8) 是下面 16 字节契约的一部分：i686 SysV 目标的 alignof(double) 只有 4，
        // 不强制对齐时 m_payload 只按 4 字节排布，JsonValue 会缩成 12 字节并破坏 static_assert。
        // x64 与 MSVC x86 的 alignof(double) 本来就是 8，加上后布局与性能没有变化。
        union alignas(8) Payload
        {
            void*         ptr;                  // 长字符串/数组/对象：指向 arena 上的描述块
            std::uint64_t u64;                  // Bool（0/1）与 Int（int64 位模式）
            double        d;                    // Double
            char          chars[8];             // InlineString：chars[0..6] 为字符，chars[7] 为长度
        };

        Payload  m_payload{};                   // 值本体（按 m_type 解释）
        JsonType m_type{JsonType::Null};        // 类型标签；成员名带 m_ 前缀，读取请用访问器

        /**
         *  @brief 取类型标签
         *  @return 当前值的 JsonType
         *  @note 字符串可能是 InlineString 或 String，判断"是不是字符串"请用 is_string()
         */
        ID_FORCEINLINE JsonType type() const noexcept { return m_type; }

        ID_FORCEINLINE bool is_null() const noexcept { return m_type == JsonType::Null; }
        ID_FORCEINLINE bool is_bool() const noexcept { return m_type == JsonType::Bool; }
        ID_FORCEINLINE bool is_int() const noexcept { return m_type == JsonType::Int; }
        ID_FORCEINLINE bool is_double() const noexcept { return m_type == JsonType::Double; }

        /**
         *  @brief 是否为字符串（含内联短串与长串两种取值）
         */
        ID_FORCEINLINE bool is_string() const noexcept
        {
            return m_type == JsonType::String || m_type == JsonType::InlineString;
        }

        ID_FORCEINLINE bool is_array() const noexcept { return m_type == JsonType::Array; }
        ID_FORCEINLINE bool is_object() const noexcept { return m_type == JsonType::Object; }

        /**
         *  @brief 取布尔值
         *  @return 是 Bool 时返回其值；其它类型返回 false
         */
        ID_FORCEINLINE bool as_bool() const noexcept
        {
            return m_type == JsonType::Bool && m_payload.u64 != 0;
        }

        /**
         *  @brief 取 64 位整数
         *  @return 是 Int 时返回该值；其它类型（含 Double）返回 0
         *  @note 不做 Int/Double 之间的隐式转换：需要 double 时先判断 is_double()
         */
        ID_FORCEINLINE std::int64_t as_int64() const noexcept
        {
            return m_type == JsonType::Int ? static_cast<std::int64_t>(m_payload.u64) : 0;
        }

        /**
         *  @brief 取双精度值
         *  @return 是 Double 时返回该值；其它类型（含 Int）返回 0.0
         *  @note 解析器只在数字含小数或指数时产生 Double，整数形态的数字是 Int
         */
        ID_FORCEINLINE double as_double() const noexcept
        {
            return m_type == JsonType::Double ? m_payload.d : 0.0;
        }

        // 以下访问器依赖 arena 上的描述块（Detail::），定义在本文件后部
        std::string_view as_string() const noexcept;

        /**
         *  @brief 取元素/成员个数
         *  @return Array 返回元素数、Object 返回成员数；其它类型返回 0
         */
        std::size_t size() const noexcept;

        /**
         *  @brief 按下标取数组元素
         *  @param index 元素下标，从 0 开始
         *  @return 是 Array 且 index 在范围内时返回元素引用；否则返回 kNullValue（不越界访问）
         */
        const JsonValue& operator[](std::size_t index) const noexcept;

        /**
         *  @brief 按键查对象成员
         *  @param key 键；按字节比较，区分大小写，不做 Unicode 规范化
         *  @return 找到时返回成员值指针，否则返回 nullptr；非 Object 时返回 nullptr
         *  @note 值指针只在文档 reset()/析构前有效
         */
        const JsonValue* find(std::string_view key) const noexcept;

        /**
         *  @brief 数组遍历（按插入序，即下标序）
         *  @return 首元素/尾后指针；非 Array 时两者都是 nullptr（可安全比较，不构成合法区间）
         */
        const JsonValue* begin() const noexcept;
        const JsonValue* end() const noexcept;

        /**
         *  @brief 对象成员遍历（按插入序；重复键只保留首次插入位置，值为最后一次出现的值）
         *  @return 首成员/尾后指针；非 Object 时两者都是 nullptr
         */
        const Detail::ObjectEntry* members_begin() const noexcept;
        const Detail::ObjectEntry* members_end() const noexcept;
    };

    // 越界或不适用时返回的空值：所有字段为零、is_null() 为 true；constexpr，无运行期全局构造
    inline constexpr JsonValue kNullValue{};

    static_assert(sizeof(JsonValue) == 16,
                  "JsonValue must stay 16 bytes on x86 and x64: scalars and short strings are inlined, "
                  "aggregates are referenced through one pointer");

    namespace Detail
    {
        /**
         *  长字符串描述块，位于 arena 上：JsonType::String 的 m_payload.ptr 指向它。
         *  data 指向字符数据：owning 解析时在 arena，non_owning 解析时可能指向输入缓冲。
         */
        struct StringRef
        {
            const char*   data{nullptr};        // 字符数据起始（不保证 NUL 结尾）
            uint32_t      length{0};            // 字符字节数
        };

        /**
         *  数组描述块，位于 arena 上：JsonType::Array 的 m_payload.ptr 指向它。
         *  items 指向 arena 上连续存放的 JsonValue；扩容按 2 倍换新块，旧块遗弃（arena 不回收单块）。
         */
        struct ArrayRef
        {
            JsonValue*    items{nullptr};       // 连续元素
            uint32_t      count{0};             // 已用元素数
            uint32_t      capacity{0};          // 已分配元素数
        };

        /**
         *  对象的一个成员；成员数组按插入序连续存放。
         */
        struct ObjectEntry
        {
            const char*   key{nullptr};         // 键字符（arena 或输入缓冲），不保证 NUL 结尾
            uint32_t      key_length{0};        // 键字节数
            JsonValue     value{};              // 成员值
        };

        /**
         *  对象描述块，位于 arena 上：JsonType::Object 的 m_payload.ptr 指向它。
         *  buckets 是幂 2 的开放寻址索引：桶存"成员下标 + 1"，0 表示空桶，线性探测；成员数不超过
         *  kObjectIndexThreshold 时不建索引（buckets 为 nullptr、bucket_mask 为 0），find 走线性扫描。
         */
        struct ObjectRef
        {
            ObjectEntry*  entries{nullptr};     // 连续成员（插入序）
            uint32_t      count{0};             // 已用成员数
            uint32_t      capacity{0};          // 已分配成员数
            uint32_t*     buckets{nullptr};     // 开放寻址索引，未建索引时为空
            uint32_t      bucket_mask{0};       // 桶数组长度 - 1
        };

        // 成员数超过该值才建索引：成员少时线性扫描更快，也不需要额外内存
        inline constexpr uint32_t kObjectIndexThreshold = 8;

        /**
         *  @brief 计算键的 32 位哈希（FNV-1a）
         *  @param key 键字节
         *  @return 哈希值；同一字节序列结果稳定
         *  @note 只用于对象内部索引，不构成对外可见的哈希契约
         */
        inline uint32_t fnv1a(std::string_view key) noexcept
        {
            uint32_t hash = 2166136261u;
            for (const char raw : key)
            {
                hash ^= static_cast<std::uint8_t>(raw);
                hash *= 16777619u;
            }
            return hash;
        }

        inline bool is_same_key(const ObjectEntry& entry, std::string_view key) noexcept
        {
            if (entry.key_length != key.size())
            {
                return false;
            }
            return key.empty() || std::memcmp(entry.key, key.data(), key.size()) == 0;
        }
    } // namespace Detail

    inline std::string_view JsonValue::as_string() const noexcept
    {
        if (m_type == JsonType::InlineString)
        {
            const auto length = static_cast<std::size_t>(static_cast<std::uint8_t>(m_payload.chars[kInlineStringCapacity]));
            return std::string_view(m_payload.chars, length);
        }
        if (m_type != JsonType::String)
        {
            return std::string_view{};
        }

        const auto* ref = static_cast<const Detail::StringRef*>(m_payload.ptr);
        if (ref == nullptr || ref->length == 0)
        {
            return std::string_view{};
        }
        return std::string_view(ref->data, ref->length);
    }

    inline std::size_t JsonValue::size() const noexcept
    {
        if (m_type == JsonType::Array)
        {
            const auto* ref = static_cast<const Detail::ArrayRef*>(m_payload.ptr);
            return ref != nullptr ? static_cast<std::size_t>(ref->count) : 0;
        }
        if (m_type == JsonType::Object)
        {
            const auto* ref = static_cast<const Detail::ObjectRef*>(m_payload.ptr);
            return ref != nullptr ? static_cast<std::size_t>(ref->count) : 0;
        }
        return 0;
    }

    inline const JsonValue& JsonValue::operator[](std::size_t index) const noexcept
    {
        if (m_type == JsonType::Array)
        {
            const auto* ref = static_cast<const Detail::ArrayRef*>(m_payload.ptr);
            if (ref != nullptr && index < ref->count)
            {
                return ref->items[index];
            }
        }
        return kNullValue;
    }

    inline const JsonValue* JsonValue::find(std::string_view key) const noexcept
    {
        if (m_type != JsonType::Object)
        {
            return nullptr;
        }

        const auto* ref = static_cast<const Detail::ObjectRef*>(m_payload.ptr);
        if (ref == nullptr || ref->count == 0)
        {
            return nullptr;
        }

        if (ref->buckets == nullptr)
        {
            // 成员少：线性扫描（插入序，连续访问，通常比哈希更快）
            for (uint32_t index = 0; index < ref->count; ++index)
            {
                if (Detail::is_same_key(ref->entries[index], key))
                {
                    return &ref->entries[index].value;
                }
            }
            return nullptr;
        }

        const uint32_t mask = ref->bucket_mask;
        uint32_t       slot = Detail::fnv1a(key) & mask;
        for (;;)
        {
            const uint32_t bucket = ref->buckets[slot];
            if (bucket == 0)
            {
                return nullptr;
            }
            const Detail::ObjectEntry& entry = ref->entries[bucket - 1];
            if (Detail::is_same_key(entry, key))
            {
                return &entry.value;
            }
            slot = (slot + 1) & mask;
        }
    }

    inline const JsonValue* JsonValue::begin() const noexcept
    {
        if (m_type != JsonType::Array)
        {
            return nullptr;
        }
        const auto* ref = static_cast<const Detail::ArrayRef*>(m_payload.ptr);
        return ref != nullptr ? ref->items : nullptr;
    }

    inline const JsonValue* JsonValue::end() const noexcept
    {
        if (m_type != JsonType::Array)
        {
            return nullptr;
        }
        const auto* ref = static_cast<const Detail::ArrayRef*>(m_payload.ptr);
        return ref != nullptr ? ref->items + ref->count : nullptr;
    }

    inline const Detail::ObjectEntry* JsonValue::members_begin() const noexcept
    {
        if (m_type != JsonType::Object)
        {
            return nullptr;
        }
        const auto* ref = static_cast<const Detail::ObjectRef*>(m_payload.ptr);
        return ref != nullptr ? ref->entries : nullptr;
    }

    inline const Detail::ObjectEntry* JsonValue::members_end() const noexcept
    {
        if (m_type != JsonType::Object)
        {
            return nullptr;
        }
        const auto* ref = static_cast<const Detail::ObjectRef*>(m_payload.ptr);
        return ref != nullptr ? ref->entries + ref->count : nullptr;
    }
} // namespace ID::Json
