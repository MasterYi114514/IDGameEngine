---
name: id-cpp-coding-style
description: Enforce the user's C++20 coding style, Allman brace indentation, compact single-line parameter signatures, column-aligned member declarations, naming conventions, and comment formatting for IDGameEngine. Use whenever writing, editing, refactoring, or reviewing any C++ (.hpp, .cpp, .ixx) or CMake code in this repository.
---

# IDGameEngine C++ Coding Style & Formatting Standard

Whenever authoring or modifying C++ files (`.hpp`, `.cpp`, `.ixx`) in `IDGameEngine`, strictly adhere to the user's established style conventions below.

---

## 1. Indentation, Braces & Line-Wrapping (Compact Allman Style)

1. **4-Space Indentation**:
   - Always use **4 spaces** per indentation level (never tabs).
   - Indent the contents inside `namespace` blocks by 4 spaces (1 level).
2. **Allman Style Braces (`{` on a new line)**:
   - Opening brace `{` goes on its own new line, aligned with the control statement or declaration (`namespace`, `class`, `struct`, `enum`, `function`, `if`, `for`, `while`, `switch`).
   - **Exception for empty constructor bodies with member initializer lists**: Place `{ }` inline at the end of the last initializer line:
     ```cpp
     constexpr LinearArena(void* buffer, size_t capacity, ArenaFallbackFn fallback = nullptr) noexcept
         : m_cursor(static_cast<uint8_t*>(buffer))
         , m_limit(static_cast<uint8_t*>(buffer) + capacity)
         , m_base(static_cast<uint8_t*>(buffer))
         , m_fallback(fallback) { }
     ```
3. **Do NOT Over-Wrap Function Signatures or Calls (Keep Signatures on a Single Line)**:
   - **Never** split function parameters or arguments onto separate lines when they fit on one line (up to ~120–140 columns).
   - ✅ **Good (User's Style)**:
     ```cpp
     ID_FORCEINLINE void init(void* buffer, size_t capacity, ArenaFallbackFn fallback = nullptr) noexcept
     ID_NOINLINE void report_assert(std::string_view expr, std::string_view message, std::string_view file, uint32_t line) const noexcept;
     m_active_sink.store(new_sink ? new_sink : &default_bootstrap_sink, std::memory_order_release);
     ```
   - ❌ **Avoid (Over-wrapped)**:
     ```cpp
     ID_FORCEINLINE void init(
         void* buffer,
         size_t capacity,
         ArenaFallbackFn fallback = nullptr
     ) noexcept
     ```
4. **Column-Aligned Member Declarations & Trailing Comments**:
   - In `struct` and `class` member blocks, align types, member names, `= default_value`, and trailing `//` comments vertically using spaces:
     ```cpp
     private:
         uint8_t*            m_cursor    = nullptr;          // 游标，指向当前可分配的内存位置
         uint8_t*            m_limit     = nullptr;          // 下限，指向缓冲区末尾，防止越界访问
         uint8_t*            m_base      = nullptr;          // 基址，指向缓冲区起始位置，用于整体重置
         ArenaFallbackFn     m_fallback  = nullptr;          // 设置回调函数
     ```
5. **Access Specifiers Alignment**:
   - Inside `class` or `struct` (indent level 1 inside `namespace`), align `public:`, `protected:`, and `private:` with `class`/`struct` (4 spaces), and indent member declarations by 1 additional level (8 spaces).
6. **Preprocessor Directives Inside Functions/Blocks**:
   - When `#if`, `#elif`, `#else`, `#endif` appear inside a function or namespace, indent them to match the current code scope, and indent their inner statements by an additional 4 spaces.

---

## 2. Naming Conventions & Standard Integer Types

| Construct | Convention | Concrete Examples |
|-----------|------------|-------------------|
| **Namespaces** | `PascalCase` (C++17 nested `A::B` allowed) | `namespace ID`, `namespace ID::Log` |
| **Classes / Structs / Enums / Type Aliases** | `PascalCase` | `LinearArena`, `SlabSpan`, `SingletonEager`, `Level`, `SinkFn` |
| **Functions & Member Methods** | `snake_case` (prefer full verbs like `allocate`) | `allocate()`, `allocate_array()`, `hash_fnv_1a64()`, `type_id<T>()` |
| **Local Variables & Parameters** | `snake_case` | `current`, `aligned`, `next`, `buffer`, `capacity` |
| **Static Member Variables** | `s_` prefix + `snake_case` | `s_instance` |
| **Class Member Variables** | `m_` prefix + `snake_case` (or `snake_case` in POD structs like `SlabSpan`) | `m_cursor`, `m_limit`, `local_free_list` |
| **Compile-Time Constants** | `k` + `PascalCase` | `kCacheLineSize`, `kSpanSize`, `kSpanMask`, `kSlabMagic` |
| **Enum Values (`enum class`)** | `PascalCase` | `Level::Trace`, `Level::Debug`, `Level::Fatal` |
| **Macros** | `ID_` prefix + `UPPER_SNAKE_CASE` | `ID_FORCEINLINE`, `ID_KERNEL_API`, `ID_DEBUG_BREAK()` |
| **Pointer & Reference Declarators** | Attach `*` and `&` to the **type** (left-aligned), no space before `(*)` in function pointers | `void* buffer`, `uint8_t* next`, `void(*)(size_t, size_t)` |
| **Fixed-Width Integer Types** | Import `<cstdint>` & `<cstddef>` in `CoreTypes.hpp` and alias them into `namespace ID` (or use `std::` / `<stdint.h>`) so IntelliSense never fails on unqualified `uint64_t` / `size_t` | `uint64_t`, `uint32_t`, `uint8_t`, `uintptr_t`, `size_t` |

---

## 3. Header Structure & Comments

1. **Header Guard & Include Grouping**:
   - Start `.hpp` files with `#pragma once`, followed by a blank line.
   - Group standard library headers (`#include <atomic>`, `#include <cstddef>`) first, followed by a blank line, then project headers (`#include "Common/Core.hpp"`, `#include "Kernel/CoreTypes.hpp"`).
2. **Closing Namespace Comment**:
   - Always annotate closing braces of namespaces with `// namespace <Name>`.
3. **Comments**:
   - Use concise Chinese comments (`// ...` for inline/trailing notes, and `/** ... */` with a 2-space indent after `*` for file-level doc blocks).
