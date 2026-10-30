#include "IDJsonDocument.hpp"

#include <cstddef>
#include <cstdlib>

/**
 *  JsonDocument 与默认段来源的实现。
 *
 *  实现说明：
 *  - 默认段来源就是 malloc/free 包装：SegmentedArena 只会以 alignof(std::max_align_t) 申请段，
 *    malloc 的保证正好覆盖它；更大的对齐由调用方换用对齐分配器（这里直接拒绝而不是返回一块
 *    对齐不满足的内存）。
 *  - 构造函数不抛异常：arena 配置失败（block_size 过小、alloc_fn 为空）只置 is_ready() 为 false，
 *    之后 parse 返回 not_initialized，而不是让构造失败或让文档处于"半初始化"状态。
 *  - 析构在头里 inline 调 arena 的 shutdown()，与构造侧使用同一个段来源，不跨分配器释放。
 */

namespace ID::Json
{
    void* default_segment_alloc(std::size_t size, std::size_t alignment) noexcept
    {
        if (alignment > alignof(std::max_align_t)) [[unlikely]]
        {
            return nullptr;
        }
        return std::malloc(size);
    }

    void default_segment_free(void* block) noexcept
    {
        std::free(block);
    }

    JsonDocument::JsonDocument(std::size_t block_size, Common::SegmentAllocFn alloc_fn,
                               Common::SegmentFreeFn free_fn) noexcept
    {
        m_ready = m_arena.init(block_size, alloc_fn, free_fn);
    }

    void JsonDocument::reset() noexcept
    {
        m_arena.reset();
        m_root  = JsonValue{};
        m_ready = m_arena.is_initialized();
    }
} // namespace ID::Json
