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

    /**
     *  @brief 向 OS 申请按 64KB 对齐的可读可写虚拟内存
     *  @param size_bytes 请求字节数，内部向上取整到 64KB
     *  @return 64KB 对齐的基址；OS 拒绝时返回 nullptr
     *  @note Windows 上 VirtualAlloc 的分配粒度本身就是 64KB。其他平台必须自己记下 mmap 的原始基址再释放，当前实现只保证 Windows
     */
    ID_KERNEL_API void* os_alloc_aligned_64k(size_t size_bytes) noexcept;

    /**
     *  @brief 把 os_alloc_aligned_64k 申请的整段映射还给 OS
     *  @param base_ptr 必须是 os_alloc_aligned_64k 的返回值，不能是用户指针
     *  @param size_bytes 与申请时相同的请求字节数，函数内部再次按 64KB 取整
     */
    ID_KERNEL_API void os_free_aligned_64k(void* base_ptr, size_t size_bytes) noexcept;

    // 获取当前线程唯一标识（直接取 TLS 变量地址，零系统调用开销）
    ID_KERNEL_API uintptr_t get_current_thread_tag() noexcept;

    // 获取当前线程的 TLS 帧线性分配器（Frame Linear Arena）
    ID_KERNEL_API LinearArena& get_thread_frame_arena() noexcept;

    // 微内核全局统一无锁分配接口（C ABI 导出，供所有插件与 IDPluginStub 劫持 new/delete 使用）
    extern "C"
    {
        ID_KERNEL_API void ID_MemoryInit() noexcept;
        ID_KERNEL_API void ID_MemoryShutdown() noexcept;

        /**
         *  @brief 统一分配入口。小于等于 8KB 且对齐不超过 8KB 时走线程局部 Slab，否则走大对象页
         *  @param size 请求字节数，0 会当成 1
         *  @param alignment 对齐字节数。小于 16 会抬到 16，必须是 2 的幂
         *  @return 对齐后的指针；对齐不是 2 的幂或 OS 耗尽时返回 nullptr
         *  @note 返回的指针才能交给 ID_AlignedFree。块内部地址、以及 64KB 对齐大对象的前一页，都不是用户指针
         */
        ID_KERNEL_API void* ID_AlignedAlloc(size_t size, size_t alignment) noexcept;

        /**
         *  @brief 按新的大小和对齐重新分配
         *  @param ptr 原指针。nullptr 等价于 ID_AlignedAlloc；新大小为 0 时释放并返回 nullptr
         *  @param new_size 新的用户字节数
         *  @param alignment 必须是 2 的幂，小于 16 会抬到 16
         *  @return 新指针。失败时原指针仍然有效，调用方继续拥有它
         *  @note Slab 块在容量和对齐都够时原地返回。大对象总是拷贝到新块
         */
        ID_KERNEL_API void* ID_AlignedRealloc(void* ptr, size_t new_size, size_t alignment) noexcept;

        /**
         *  @brief 释放 ID_AlignedAlloc 或 ID_AlignedRealloc 返回的指针
         *  @param ptr 原指针。nullptr 直接返回
         *  @note 同一指针释放两次、或者传入块内部地址，会破坏空闲链表。大对象按页首 magic 识别，64KB 对齐的用户指针从前一页找头
         */
        ID_KERNEL_API void ID_AlignedFree(void* ptr) noexcept;

        ID_KERNEL_API MemoryStats ID_GetMemoryStats() noexcept;
    }
} // namespace ID::Memory
