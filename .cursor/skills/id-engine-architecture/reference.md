# IDGameEngine Architecture Reference & Code Templates

This file contains canonical C++20 and CMake templates for the three-tier peer repository layout (`ID/`, `IDPlugin/`, `Sandbox/`), following the project's compact Allman + `snake_case` coding standard.

---

## 1. Three-Tier CMake Build System Templates

### 1.1 Root `CMakeLists.txt` (`F:/IDGameEngine/CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.20)

project(IDGameEngine
    LANGUAGES CXX
    VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ID_MONOLITHIC_BUILD "静态归并所有插件并开启全程序优化 (LTO/LTCG)" OFF)
option(ID_ENABLE_AVX2      "开启 AVX2/FMA SIMD 指令集优化" ON)

set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR}/bin)

# Common extreme-performance compiler flags
function(id_apply_performance_flags TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4 /GR- /EHs-c-
            $<$<CONFIG:Release>:/O2 /Oi /Ot /Ob3 /Gw /Gy /fp:fast>
        )
        if(ID_ENABLE_AVX2)
            target_compile_options(${TARGET_NAME} PRIVATE /arch:AVX2)
        endif()
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -fno-rtti -fno-exceptions
            $<$<CONFIG:Release>:-O3 -ffast-math -fomit-frame-pointer>
        )
        if(ID_ENABLE_AVX2)
            target_compile_options(${TARGET_NAME} PRIVATE -mavx2 -mfma)
        endif()
    endif()

    if(ID_MONOLITHIC_BUILD)
        set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

add_subdirectory(ID)
add_subdirectory(IDPlugin)
add_subdirectory(Sandbox)
```

### 1.2 Microkernel `ID/CMakeLists.txt`

```cmake
set(ID_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE ID_KERNEL_SRC ${ID_DIR}/include/*.hpp ${ID_DIR}/src/Kernel/*.cpp)

if(ID_MONOLITHIC_BUILD)
    add_library(ID STATIC ${ID_KERNEL_SRC})
    target_compile_definitions(ID PUBLIC ID_STATIC_PLUGIN_LINK=1)
else()
    add_library(ID SHARED ${ID_KERNEL_SRC})
    target_compile_definitions(ID PRIVATE ID_KERNEL_BUILD_DLL=1)
endif()

target_include_directories(ID PUBLIC ${ID_DIR}/include)
id_apply_performance_flags(ID)
```

### 1.3 Plugin Hub `IDPlugin/CMakeLists.txt`

```cmake
set(ID_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 1. Static memory hijack stub privately linked into every plugin and executable
add_library(IDPluginStub STATIC ${ID_PLUGIN_DIR}/Stub/IDPluginStub.cpp)
target_include_directories(IDPluginStub PUBLIC ${ID_PLUGIN_DIR}/include)
target_link_libraries(IDPluginStub PUBLIC ID)
id_apply_performance_flags(IDPluginStub)

# 2. Unified macro for defining an IDGameEngine plugin
function(id_add_plugin PLUGIN_NAME)
    cmake_parse_arguments(PLUGIN "INLINE_ONLY" "" "SOURCES;DEPENDS" ${ARGN})

    if(PLUGIN_INLINE_ONLY)
        add_library(${PLUGIN_NAME} INTERFACE)
        target_include_directories(${PLUGIN_NAME} INTERFACE ${ID_PLUGIN_DIR}/include)
        target_link_libraries(${PLUGIN_NAME} INTERFACE ID ${PLUGIN_DEPENDS})
        return()
    endif()

    if(ID_MONOLITHIC_BUILD)
        add_library(${PLUGIN_NAME} STATIC ${PLUGIN_SOURCES})
        set_property(TARGET ${PLUGIN_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})
        target_compile_definitions(${PLUGIN_NAME} PRIVATE ID_PLUGIN_BUILD_DLL=1)
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC ${ID_PLUGIN_DIR}/include)
    id_apply_performance_flags(${PLUGIN_NAME})
    target_link_libraries(${PLUGIN_NAME} PUBLIC ID ${PLUGIN_DEPENDS})
    target_link_libraries(${PLUGIN_NAME} PRIVATE IDPluginStub)
endfunction()
```

### 1.4 Host & Test `Sandbox/CMakeLists.txt`

```cmake
file(GLOB_RECURSE SANDBOX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(Sandbox ${SANDBOX_SRC})
target_link_libraries(Sandbox PRIVATE ID IDPluginStub)
id_apply_performance_flags(Sandbox)
```

---

## 2. Plugin Static Memory Hijack Stub (`IDPlugin/Stub/IDPluginStub.cpp`)

Link `IDPluginStub` privately into every DLL plugin and `Sandbox` so `new`/`delete` always routes to `ID.Kernel`'s lock-free Slab/Page allocator:

```cpp
#include <new>

#include "Kernel/Memory.hpp"

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
```

---

## 3. Declarative Plugin Descriptor & DAG Lifecycle (`ID/include/Kernel/Plugin.hpp`)

```cpp
#pragma once

#include <span>
#include <string_view>

#include "Common/Core.hpp"
#include "Common/templates/Singleton.hpp"
#include "Kernel/CoreTypes.hpp"

namespace ID
{
    enum class ServiceSlot : uint32_t
    {
        Log         = 0,
        Containers  = 1,
        Math        = 2,
        JobSystem   = 3,
        ECS         = 4,
        Physics     = 5,
        Render      = 6,
        Audio       = 7,
        Gameplay    = 8,
        MaxSlots    = 64
    };

    struct PluginDependency
    {
        uint64_t    plugin_id   = 0;
        uint32_t    min_version = 1;
    };

    struct PluginDescriptor
    {
        uint64_t                            plugin_id       = 0;
        uint32_t                            version         = 1;
        std::string_view                    name            = {};
        std::span<const PluginDependency>   dependencies    = {};

        void(*on_register)() noexcept   = nullptr;
        bool(*on_init)() noexcept       = nullptr;
        void(*on_shutdown)() noexcept   = nullptr;
        void(*on_unload)() noexcept     = nullptr;
    };
} // namespace ID
```
