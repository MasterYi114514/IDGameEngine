#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace ID
{
    // 将标准定长整型与指针类型引入 ID 命名空间，确保在 namespace ID 内直接写 uint64_t / size_t 符合 C++ 标准且不触发 IntelliSense 报错
    using std::uint8_t;
    using std::uint16_t;
    using std::uint32_t;
    using std::uint64_t;
    using std::int8_t;
    using std::int16_t;
    using std::int32_t;
    using std::int64_t;
    using std::uintptr_t;
    using std::intptr_t;
    using std::size_t;
    using std::ptrdiff_t;
    using std::max_align_t;

    // CPU 的 L1 缓存行大小，通常为 64 字节
    inline constexpr size_t kCacheLineSize = 64;

    // consteval 的 FNV-1a 哈希函数
    consteval uint64_t hash_fnv_1a64(std::string_view str) noexcept
    {
        uint64_t hash = 14695981039346656037ull; // FNV-1a 64 位偏移基数
        for (char c : str)
        {
            hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
            hash *= 1099511628211ull; // FNV-1a 64 位质数
        }
        return hash;
    }

    template <typename T>
    consteval uint64_t type_id() noexcept
    {
        #if defined(_MSC_VER)
            return hash_fnv_1a64(__FUNCSIG__);
        #elif defined(__clang__) || defined(__GNUC__)
            return hash_fnv_1a64(__PRETTY_FUNCTION__);
        #else
            static_assert(false, "Unsupported compiler for type_id");
        #endif
    }

    // 编译期检查
    static_assert(type_id<int32_t>() != type_id<uint32_t>(), "type_id<int32_t> should not equal type_id<uint32_t>");
    static_assert(type_id<float>() != type_id<double>(), "type_id<float> should not equal type_id<double>");
} // namespace ID
