#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

/**
 *  CPU 从来不直接执行内存里的代码。任何函数都要先被取指单元从内存搬进L1 指令缓存（L1ICache），然后才能执行。注意 L1ICache 和 L1DCache 是分开的，CPU 取指和读写数据是两个独立的流水线。有以下几个关键的事实：
 *  1. 代码 是以 cache line 为单位被搬进来的，一般 cache line 是 64 字节
 *  2. 取指带宽是流水线的瓶颈。Legacy decode 路径 x86 典型 16 字节/周期，而现代 CPU 还有一层更小的 µop cache（Intel DSB / AMD Op Cache，只存数千条已解码 µop）：小循环能从 µop cache 直接喂满 5~6 µop/周期；一旦函数太大或控制流太复杂，就回退到慢的 decode 路径
 *  3. 取指 miss 和 L1I 抢占会直接变成前端 stall（VTune 里叫 Front-End Bound），表现为 IPC 掉、帧时间抖动——而且它和数据 miss 不同，加时钟频率和更大的 L3 都救不回来多少，只能靠代码布局
 * 
 *  因此，IDGameEngine 在设计上尽量实现冷热分离，即把函数分为冷路径（使用频率低）和热路径（使用频率高），让热路径的代码尽量集中在一起，避免被冷路径污染 L1ICache；冷路径的代码从物理上被移到 `.text.unlikely` 之类的地方。
 *  - 宏 `ID_COLD` 可以标记函数为冷路径（对 MSVC 编译器无效）
 *  - 宏 `ID_NOINLINE` 标记函数不内联，确定其必然有一次调用开销，但是可以把函数体从热路径剥离出去，避免污染 L1ICache。
 *      - 此宏一般跟 `ID_COLD` 一起使用，因为二者的目标函数都是冷路径函数。
 *      - 对于很小、很热的函数，不应该使用 `ID_NOINLINE`，因为它会增加函数调用开销
 *  - 宏 `ID_FORCEINLINE` 标记函数强制内联，避免函数调用开销。对于很小、很热的函数，应该使用 `ID_FORCEINLINE`。
 */

namespace ID
{
    // 将标准定长整型与指针类型引入 ID 命名空间
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

    /**
     *  @brief 编译期类型标识符生成函数，可以做到在编译期为每个类型生成唯一的标识符。函数在编译期执行，因此不会引入运行时开销。
     *  @tparam T 要生成类型标识符的类型。
     *  @return 返回一个唯一的 64 位整数，表示类型 T 的标识符。
     *  @note 该函数利用编译器提供的类型信息（如 __FUNCSIG__ 或 __PRETTY_FUNCTION__）来生成类型的唯一标识符；因此，虽然函数在不同编译器下获得的 type_id 可能有所不同，但都能保证在同一编译器下，生成的标识符是唯一的。
     */
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
