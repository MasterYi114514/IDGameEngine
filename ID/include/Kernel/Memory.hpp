#pragma once

#include "Common/Core.hpp"
#include "Common/tools/LinearArena.hpp"
#include "Common/tools/SlabSpan.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  微内核全局统一无锁内存分配系统（ID::Memory）
 *  负责管理 OS 64KB 对齐虚拟内存页（Page Heap）、15 档无锁 SlabSpan 线程局部调度池，以及 TLS 帧线性分配器。
 */

namespace ID::Memory
{
    inline constexpr size_t kNumSizeClasses    = 15;                // 小对象 Size Class 总档位数
    inline constexpr size_t kMaxSmallBlockSize = 8192;              // 小对象最大尺寸（8KB），超过此尺寸走大对象 64KB 页分配
    inline constexpr size_t kFrameArenaSize    = 4 * 1024 * 1024;   // 每线程默认 4MB 帧线性分配缓冲区

    // 15 档小对象块大小配置表（单位：字节）
    inline constexpr uint32_t kSizeClasses[kNumSizeClasses] =
    {
        16, 32, 48, 64, 96, 128, 192, 256,
        384, 512, 768, 1024, 2048, 4096, 8192
    };

    struct MemoryStats
    {
        size_t total_reserved_bytes;    // 向 OS 申请的 64KB 对齐虚拟内存总字节数
        size_t active_slab_count;       // 当前已创建的 64KB SlabSpan 数量
        size_t large_allocation_bytes;  // 大对象（> 8KB）当前占用的字节数
    };

    // OS 64KB 对齐虚拟内存页底层接口
    ID_KERNEL_API void* os_alloc_aligned_64k(size_t size_bytes) noexcept;
    ID_KERNEL_API void  os_free_aligned_64k(void* base_ptr, size_t size_bytes) noexcept;

    // 获取当前线程唯一标识（直接取 TLS 变量地址，零系统调用开销）
    ID_KERNEL_API uintptr_t get_current_thread_tag() noexcept;

    // 获取当前线程的 TLS 帧线性分配器（Frame Linear Arena）
    ID_KERNEL_API LinearArena& get_thread_frame_arena() noexcept;

    // 微内核全局统一无锁分配接口（C ABI 导出，供所有插件与 IDPluginStub 劫持 new/delete 使用）
    extern "C"
    {
        ID_KERNEL_API void        ID_MemoryInit() noexcept;
        ID_KERNEL_API void        ID_MemoryShutdown() noexcept;
        ID_KERNEL_API void*       ID_AlignedAlloc(size_t size, size_t alignment) noexcept;
        ID_KERNEL_API void*       ID_AlignedRealloc(void* ptr, size_t new_size, size_t alignment) noexcept;
        ID_KERNEL_API void        ID_AlignedFree(void* ptr) noexcept;
        ID_KERNEL_API MemoryStats ID_GetMemoryStats() noexcept;
    }
} // namespace ID::Memory
