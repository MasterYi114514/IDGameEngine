---
name: id-engine-architecture
description: Design, implement, and optimize the IDGameEngine C++20 microkernel and zero-overhead plugin architecture. Use when developing or modifying ID.Kernel, creating or registering engine plugins (Math, Containers, Log, JobSystem, ECS, Physics, Render, Gameplay), configuring CMake build targets, managing memory allocators, or optimizing runtime performance.
---

# IDGameEngine Microkernel & Zero-Overhead Plugin Architecture

This skill defines the architectural mandates, workflows, and engineering patterns for **IDGameEngine**—a pure C++20 game engine built on three non-negotiable pillars:

1. **Microkernel Architecture (`ID.Kernel`)**: The kernel retains only minimal foundational capabilities (plugin lifecycle & DAG dependency manager, global lock-free memory allocator + TLS frame arena, compile-time `TypeID` / fixed-slot service registry, and bootstrap fallback hooks).
2. **Everything is a Plugin**: All low-level and domain systems (Math, Containers, Log, JobSystem, ECS, Physics, Render, Gameplay) are plugins with explicit dependency declarations (DAG).
3. **Extreme Runtime Efficiency**: Trade compile/link time for near-zero runtime overhead. Eliminate cross-DLL inlining barriers, virtual method tables (`vtable`), runtime string lookups, heap fragmentation, and cache misses.

---

## Architectural Iron Laws (Mandatory Constraints)

When writing or reviewing any C++ or CMake code in this repository, enforce these rules without exception:

- **No RTTI & No Exceptions**: Always compile with `/GR- /EHs-c-` (MSVC) or `-fno-rtti -fno-exceptions` (Clang/GCC). Use compile-time 64-bit FNV-1a `ID::TypeID<T>()` (`consteval`) instead of `typeid` or `dynamic_cast`.
- **Zero Dynamic Static Initialization (`constinit`)**: Global or static variables with runtime constructors are strictly forbidden (prevents Static Initialization Order Fiasco across plugins). All global state must be `constexpr` or `constinit`.
- **No Cross-Plugin Virtual Interfaces (`vtable`)**: Never expose `class IPluginService { virtual void Foo() = 0; }` across plugin boundaries. Use direct inline symbols in Monolithic LTO builds and flat `constinit` C-API function pointer structs in DLL builds.
- **No Per-Object Cross-Plugin Calls in Hot Loops**: Cross-plugin APIs on hot paths must operate on contiguous memory batches (`std::span<T>` or Structure-of-Arrays pointers with `__restrict`), never one object at a time.
- **Unified Kernel Memory Heap**: Every plugin must route all allocations through `ID.Kernel` (via `IDPluginStub` overriding global `operator new`/`delete` and explicit `ID::Allocator` concepts). Never mix independent CRT heaps across DLLs.

---

## Plugin Tier Classification & Dual-Build Strategy

Before creating or modifying a plugin, classify it into the correct physical tier:

| Tier | Representative Plugins | Call Frequency | Dev Build (`ID_MONOLITHIC_BUILD=OFF`) | Release Build (`ID_MONOLITHIC_BUILD=ON`) |
|------|------------------------|----------------|---------------------------------------|------------------------------------------|
| **Tier 0** | `ID.Kernel`, `ID.PluginStub` | Startup / TLS per-frame | `IDKernel.dll` + `IDPluginStub.lib` | Static monolithic core + `/LTCG` |
| **Tier 1** | `ID.Math`, `ID.Containers`, `ID.Log` (Frontend) | Millions/frame | C++20 Module (`.ixx`) / Header-only (`INTERFACE`) | 100% force-inlined (`__forceinline` + `__vectorcall`) |
| **Tier 2** | `ID.JobSystem`, `ID.ECS`, `ID.Physics`, `ID.Render` | Tens–Thousands/frame | Independent `.dll` with flat C-API table | `STATIC` lib merged via Full LTO (`/GL` + `/LTCG`) |
| **Tier 3** | `ID.Gameplay`, Editor/Tools | Per-frame stage | Hot-reloadable `.dll` (stateless logic) | `STATIC` lib merged via Full LTO + PGO |

---

## Core Workflows

### Workflow 1: Creating a New Self-Contained Plugin & Multi-Backend Compatibility

All plugins live as self-contained subprojects under `IDPlugin/<PluginName>/` (alongside `ID/` for the microkernel and `Sandbox/` for the host executable). Multiple alternative plugins for the same role (e.g., `IDMath` vs. `IDMath_Fixed`, `IDRender_Vulkan` vs. `IDRender_DX12`) are switched at configure/compile time with zero runtime overhead:

```markdown
Plugin Creation Progress:
- [ ] Step 1: Determine plugin tier (Tier 1 Inline/Module vs Tier 2/3 Dual-Build) and role alias (e.g., `ID::Math`)
- [ ] Step 2: Create self-contained directory `IDPlugin/<PluginName>/` with its own `CMakeLists.txt`, `include/<PluginName>/Core.hpp`, and `src/`
- [ ] Step 3: Declare `consteval` Plugin ID, `compatible_ids` dependency whitelist, and `PluginDescriptor`
- [ ] Step 4: Expose API and register via `id_add_plugin(<PluginName> ROLE_ALIAS <Role> ...)`
- [ ] Step 5: In downstream plugins, handle upstream differences via `#if defined(ID_USE_PLUGIN_<UPPER_NAME>) ... #else static_assert(false)`
```

1. **Self-Contained Layout & Identity Macro**:
   - `id_add_plugin(<PluginName>)` automatically exports `PUBLIC` compile definition `ID_USE_PLUGIN_<UPPER_NAME>=1` to the plugin and all downstream dependents, and optionally binds `ROLE_ALIAS` (e.g., `ID::Math`).
2. **Downstream Multi-Upstream Compatibility (Method B)**:
   - Upstream plugins are **not** forced into a lowest-common-denominator API; each plugin exposes its full hardware/algorithmic strengths.
   - Downstream plugins (e.g., `ID.ECS`, `ID.Physics`) adapt to differences across multiple supported upstream plugins using `#if defined(ID_USE_PLUGIN_IDMATH) ... #elif defined(ID_USE_PLUGIN_IDMATH_FIXED) ... #else static_assert(false, "Unsupported upstream plugin!"); #endif`.
   - In `PluginDependency`, downstream plugins pass `compatible_ids` (a `std::span<const uint64_t>` whitelist of all supported upstream plugin IDs). `PluginManager::initialize_all()` verifies that at least one compatible upstream is registered and meets `min_version`; otherwise assembly is rejected immediately.
3. **Lifecycle Registration**:
   - Implement the four symmetrical lifecycle callbacks:
     - `on_register() noexcept`: Register `type_id` layouts, memory slab sizes, and publish the plugin's API function table into its fixed kernel slot (`PluginManager::instance().register_service(...)`).
     - `on_init() noexcept`: Initialize subsystem resources (guaranteed by Kernel Kahn topological sort to run *after* all declared dependencies have initialized).
     - `on_shutdown() noexcept`: Stop worker tasks and release subsystem resources (runs in strict *reverse* topological order).
     - `on_unload() noexcept`: Unbind API table pointers and reset to fallback/null stubs.

### Workflow 2: Designing Zero-Overhead Cross-Plugin Communication

1. **Service Addressing**:
   - Assign a compile-time slot in `ID::ServiceSlot` or use `ID::TypeID<ApiTable>()` to index directly into the kernel's `alignas(64)` flat service array (`1` CPU instruction, zero string hashing at runtime).
2. **Dual-Mode Dispatch Header Pattern**:
   - When `ID_STATIC_PLUGIN_LINK` is defined (Release Monolithic LTO), declare plugin APIs as direct functions so the linker inlines them across plugins.
   - When `ID_STATIC_PLUGIN_LINK` is not defined (Dev DLL), route inline wrappers through a `constinit` function pointer table populated during `OnRegister()`.
3. **SIMD & Register Passing**:
   - Mark pure mathematical and physics functions with `__vectorcall` (MSVC/Clang on Windows) so `Vec4`/`Mat4` arguments pass directly in `XMM`/`YMM` registers without stack spills.

### Workflow 3: Solving Bootstrap & Hot-Reload Challenges

1. **Bootstrap "Chicken-and-Egg" (e.g., `ID.Log` vs `ID.Kernel`)**:
   - `ID.Kernel` must never link against `ID.Log` at compile time.
   - `ID.Kernel` provides an atomic function pointer `g_BootstrapLogSink` initialized (`constinit`) to a 10-line OS fallback (`OutputDebugStringA` / `stderr`) plus a small lock-free ring buffer.
   - When `ID.Log` loads in Rank 1, its `OnRegister()` atomically swaps `g_BootstrapLogSink` to the high-performance async ring-buffer logger and flushes early messages.
2. **Cold Path Isolation for Logging & Asserts**:
   - Wrap all log/assert macro branches with `[[unlikely]]` and move formatting/dispatch call sites into `__declspec(noinline)` cold functions so the CPU L1 instruction cache (I-Cache) in hot loops stays unpolluted.
3. **Stateless Plugin Hot-Reload**:
   - Store persistent runtime state in `ID.ECS` contiguous component pages or `ID.Kernel` blackboard memory rather than static variables inside Tier 2/3 plugins. This allows unloading and reloading a plugin `.dll` in Dev mode without losing world state.

---

## Reference Material

For concrete C++20 header templates, memory allocator stubs, DAG topological sort implementations, and CMake LTO/LTCG configurations, read [reference.md](reference.md).
