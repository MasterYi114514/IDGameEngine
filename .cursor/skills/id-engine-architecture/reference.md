# IDGameEngine Architecture Reference & Code Templates

This file contains canonical C++20 and CMake templates for implementing `ID.Kernel`, `ID.PluginStub`, and engine plugins.

---

## 1. CMake Dual-Build & Extreme Optimization Template (`ID/CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.20)

project(ID
    VERSION 0.0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ID_MONOLITHIC_BUILD "Merge all plugins statically with Full LTO/LTCG for maximum runtime speed" ON)
option(ID_ENABLE_UNITY_BUILD "Enable Unity/Jumbo builds to accelerate compilation and intra-plugin inlining" ON)
option(ID_ENABLE_AVX2 "Enable AVX2/FMA SIMD instruction sets" ON)

set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(ID_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR}/bin)

# Common extreme-performance compiler flags
function(id_apply_performance_flags TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4 /WX
            /GR-                # Disable RTTI
            /EHs-c-             # Disable C++ Exceptions
            /GS-                # Disable buffer security check in hot loops (Release)
            $<$<CONFIG:Release>:/O2 /Oi /Ot /Ob3 /GL /Gw /Gy /fp:fast>
        )
        if(ID_ENABLE_AVX2)
            target_compile_options(${TARGET_NAME} PRIVATE /arch:AVX2)
        endif()
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        )
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Werror
            -fno-rtti
            -fno-exceptions
            $<$<CONFIG:Release>:-O3 -flto=full -ffast-math -fomit-frame-pointer -ffunction-sections -fdata-sections>
        )
        if(ID_ENABLE_AVX2)
            target_compile_options(${TARGET_NAME} PRIVATE -mavx2 -mfma)
        endif()
    endif()
endfunction()

# Unified macro for defining an IDGameEngine plugin
function(id_add_plugin PLUGIN_NAME)
    cmake_parse_arguments(PLUGIN "INLINE_ONLY" "" "SOURCES;HEADERS;DEPENDS" ${ARGN})

    if(PLUGIN_INLINE_ONLY)
        add_library(${PLUGIN_NAME} INTERFACE)
        target_include_directories(${PLUGIN_NAME} INTERFACE ${ID_DIR}/include)
        target_link_libraries(${PLUGIN_NAME} INTERFACE IDKernel ${PLUGIN_DEPENDS})
        return()
    endif()

    if(ID_MONOLITHIC_BUILD)
        add_library(${PLUGIN_NAME} STATIC ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})
        set_property(TARGET ${PLUGIN_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        target_compile_definitions(${PLUGIN_NAME} PUBLIC ID_STATIC_PLUGIN_LINK=1)
    else()
        add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})
        target_compile_definitions(${PLUGIN_NAME} PRIVATE ID_PLUGIN_BUILD_DLL=1)
    endif()

    if(ID_ENABLE_UNITY_BUILD)
        set_target_properties(${PLUGIN_NAME} PROPERTIES UNITY_BUILD ON)
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC ${ID_DIR}/include)
    id_apply_performance_flags(${PLUGIN_NAME})
    target_link_libraries(${PLUGIN_NAME} PUBLIC IDKernel ${PLUGIN_DEPENDS})
    target_link_libraries(${PLUGIN_NAME} PRIVATE IDPluginStub)
endfunction()
```

---

## 2. Compile-Time TypeID & Platform Macros (`ID/include/Kernel/CoreTypes.hpp`)

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

#if defined(_MSC_VER)
    #define ID_FORCEINLINE __forceinline
    #define ID_NOINLINE    __declspec(noinline)
    #define ID_VECTORCALL  __vectorcall
    #define ID_RESTRICT    __restrict
#elif defined(__clang__) || defined(__GNUC__)
    #define ID_FORCEINLINE inline __attribute__((always_inline))
    #define ID_NOINLINE    __attribute__((noinline, cold))
    #define ID_VECTORCALL  __vectorcall
    #define ID_RESTRICT    __restrict__
#else
    #define ID_FORCEINLINE inline
    #define ID_NOINLINE
    #define ID_VECTORCALL
    #define ID_RESTRICT
#endif

#if defined(ID_STATIC_PLUGIN_LINK)
    #define ID_API
#elif defined(_WIN32)
    #if defined(ID_KERNEL_BUILD_DLL) || defined(ID_PLUGIN_BUILD_DLL)
        #define ID_API __declspec(dllexport)
    #else
        #define ID_API __declspec(dllimport)
    #endif
#else
    #define ID_API __attribute__((visibility("default")))
#endif

namespace ID {

    inline constexpr size_t kCacheLineSize = 64;

    consteval uint64_t HashFNV1a64(std::string_view str) noexcept {
        uint64_t hash = 14695981039346656037ull;
        for (char c : str) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    template <typename T>
    consteval uint64_t TypeID() noexcept {
    #if defined(_MSC_VER)
        return HashFNV1a64(__FUNCSIG__);
    #else
        return HashFNV1a64(__PRETTY_FUNCTION__);
    #endif
    }

} // namespace ID
```

---

## 3. Kernel Memory Allocator & TLS Frame Arena (`ID/include/Kernel/Memory.hpp`)

```cpp
#pragma once
#include "Kernel/CoreTypes.hpp"
#include <new>

namespace ID::Memory {

    // Exported by ID.Kernel: lock-free slab/page allocator entry points
    extern "C" {
        ID_API void* ID_AlignedAlloc(size_t size, size_t alignment) noexcept;
        ID_API void* ID_AlignedRealloc(void* ptr, size_t newSize, size_t alignment) noexcept;
        ID_API void  ID_AlignedFree(void* ptr) noexcept;
    }

    // Zero-call-overhead per-thread frame linear allocator
    struct alignas(kCacheLineSize) FrameArena {
        uint8_t* cursor = nullptr;
        uint8_t* limit  = nullptr;
        uint8_t* base   = nullptr;

        ID_FORCEINLINE void* Alloc(size_t size, size_t alignment = 16) noexcept {
            const uintptr_t aligned = (reinterpret_cast<uintptr_t>(cursor) + (alignment - 1)) & ~(alignment - 1);
            uint8_t* next = reinterpret_cast<uint8_t*>(aligned + size);
            if (next > limit) [[unlikely]] {
                return nullptr; // Fallback to kernel page allocation if needed
            }
            cursor = next;
            return reinterpret_cast<void*>(aligned);
        }

        ID_FORCEINLINE void Reset() noexcept {
            cursor = base;
        }
    };

    // Direct TLS access: 1 instruction on fast path
    extern ID_API thread_local FrameArena g_TlsFrameArena;

} // namespace ID::Memory
```

---

## 4. Plugin Static Memory Hijack Stub (`ID/PluginStub/src/IDPluginStub.cpp`)

Link `IDPluginStub` privately into every DLL plugin so `new`/`delete` inside STL or plugin code always routes to `ID.Kernel`'s unified heap:

```cpp
#include "Kernel/Memory.hpp"
#include <new>

void* operator new(size_t size) {
    return ID::Memory::ID_AlignedAlloc(size, alignof(std::max_align_t));
}
void* operator new[](size_t size) {
    return ID::Memory::ID_AlignedAlloc(size, alignof(std::max_align_t));
}
void* operator new(size_t size, std::align_val_t al) {
    return ID::Memory::ID_AlignedAlloc(size, static_cast<size_t>(al));
}
void* operator new[](size_t size, std::align_val_t al) {
    return ID::Memory::ID_AlignedAlloc(size, static_cast<size_t>(al));
}

void operator delete(void* ptr) noexcept {
    ID::Memory::ID_AlignedFree(ptr);
}
void operator delete[](void* ptr) noexcept {
    ID::Memory::ID_AlignedFree(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    ID::Memory::ID_AlignedFree(ptr);
}
void operator delete[](void* ptr, size_t) noexcept {
    ID::Memory::ID_AlignedFree(ptr);
}
void operator delete(void* ptr, std::align_val_t) noexcept {
    ID::Memory::ID_AlignedFree(ptr);
}
void operator delete[](void* ptr, std::align_val_t) noexcept {
    ID::Memory::ID_AlignedFree(ptr);
}
```

---

## 5. Declarative Plugin Descriptor & DAG Lifecycle (`ID/include/Kernel/Plugin.hpp`)

```cpp
#pragma once
#include "Kernel/CoreTypes.hpp"
#include <span>

namespace ID {

    struct PluginDependency {
        uint64_t pluginId;
        uint32_t minVersion;
    };

    struct PluginDescriptor {
        uint64_t pluginId;
        uint32_t version;
        std::string_view name;
        std::span<const PluginDependency> dependencies;

        // 4-Phase Symmetrical Lifecycle (No exceptions, no virtual table)
        void (*OnRegister)() noexcept; // Phase 1: Register TypeIDs, memory pools, and C-API tables
        bool (*OnInit)() noexcept;     // Phase 2: Initialize subsystem (dependencies guaranteed ready)
        void (*OnShutdown)() noexcept; // Phase 3: Reverse topological shutdown (stop jobs/threads)
        void (*OnUnload)() noexcept;   // Phase 4: Reverse topological unload (unpublish C-API tables)
    };

    // Fixed compile-time slots for core engine services (O(1) single-instruction array indexing)
    enum class ServiceSlot : uint32_t {
        Log       = 0,
        JobSystem = 1,
        ECS       = 2,
        Physics   = 3,
        Render    = 4,
        Audio     = 5,
        Gameplay  = 6,
        MaxSlots  = 64
    };

    struct alignas(kCacheLineSize) KernelServiceTable {
        const void* slots[static_cast<size_t>(ServiceSlot::MaxSlots)] = {nullptr};
    };

    extern ID_API constinit KernelServiceTable g_ServiceTable;

    template <typename ApiTableType>
    ID_FORCEINLINE const ApiTableType* GetService(ServiceSlot slot) noexcept {
        return static_cast<const ApiTableType*>(g_ServiceTable.slots[static_cast<size_t>(slot)]);
    }

} // namespace ID
```
