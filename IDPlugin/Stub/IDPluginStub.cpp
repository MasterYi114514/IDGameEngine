#include <new>

#include "Kernel/Memory.hpp"

/**
 *  插件静态内存劫持桩（IDPluginStub）
 *  强制以 PRIVATE 方式静态链接至每个插件与可执行文件中，将所有全局 new / delete 统一路由至微内核无锁 Slab/Page 内存池。
 */

void* operator new(std::size_t size)
{
    return ID::Memory::ID_AlignedAlloc(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size)
{
    return ID::Memory::ID_AlignedAlloc(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return ID::Memory::ID_AlignedAlloc(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return ID::Memory::ID_AlignedAlloc(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return ID::Memory::ID_AlignedAlloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ID::Memory::ID_AlignedAlloc(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return ID::Memory::ID_AlignedAlloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return ID::Memory::ID_AlignedAlloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete[](void* ptr) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept
{
    ID::Memory::ID_AlignedFree(ptr);
}
