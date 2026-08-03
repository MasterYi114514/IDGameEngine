#pragma once

#include <cstdint>
#include <cstddef>

namespace ID
{
    class ArenaManager; // 前向声明 ArenaManager 类

    using ArenaIDType = uint32_t;

    /**
     *  不允许外界创建某个有效值的 ArenaID 实例，也不允许直接给 ArenaID 赋值
     *  只允许通过 ArenaManager 创建 ArenaID 有效实例
     *  允许 ArenaID 正常拷贝、移动进行传播
     */
    class ArenaID
    {
    public:
        ArenaID() : m_id(static_cast<ArenaIDType>(-1)) {}
        ~ArenaID() = default;
        
        ArenaID(const ArenaID&) = default;
        ArenaID& operator=(const ArenaID&) = default;
        ArenaID(ArenaID&&) = default;
        ArenaID& operator=(ArenaID&&) = default;

        static ArenaID invalid_id() { return static_cast<ArenaIDType>(-1); }
        const ArenaIDType& get_id() const { return m_id; }

    private:
        // 私有构造函数，仅允许友元 ArenaManager 创建 ArenaID 实例
        friend class ArenaManager;
        ArenaID(ArenaIDType id) : m_id(id) {}

        ArenaIDType m_id;
    };

    class ArenaManager
    {
    public:
        ArenaManager() = delete;
        ~ArenaManager() = delete;

    public:
        static constexpr size_t DEFAULT_CAPACITY = 4096;    // 默认容量为 4KB
        static ArenaID  create_arena(size_t size = DEFAULT_CAPACITY);

        static void     destroy_arena(ArenaID arena_id);
        static void     reset_arena(ArenaID arena_id);
    };
} // namespace ID