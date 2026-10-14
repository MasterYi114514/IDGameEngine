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

### Workflow 1: Creating a New Plugin

Copy and follow this checklist when adding a new plugin under `ID/Plugins/<PluginName>/`:

```markdown
Plugin Creation Progress:
- [ ] Step 1: Determine plugin tier (Tier 1 Inline/Module vs Tier 2/3 Dual-Build)
- [ ] Step 2: Create headers under `ID/include/<PluginName>/` and sources under `ID/Plugins/<PluginName>/src/`
- [ ] Step 3: Declare `consteval` Plugin ID, dependencies, and `PluginDescriptor`
- [ ] Step 4: Expose API (Header/Module for Tier 1; dual-mode flat API table for Tier 2/3)
- [ ] Step 5: Register plugin target via `id_add_plugin()` in CMake
```

1. **Determine Tier**:
   - **If called inside inner loops (Math, SIMD, Containers, Hashing)**: Use `INLINE_ONLY` in `id_add_plugin()`. Never export small hot functions across a non-inlined library boundary.
   - **If a subsystem (Log backend, ECS, Physics, Render)**: Use standard dual-build `id_add_plugin()` so it builds as `.dll` in Dev and `.lib` with LTO in Release.
2. **Lifecycle Registration**:
   - Implement the four symmetrical lifecycle callbacks:
     - `OnRegister() noexcept`: Register `TypeID` layouts, memory slab sizes, and publish the plugin's API function table into its fixed kernel slot.
     - `OnInit() noexcept`: Initialize subsystem resources (guaranteed by Kernel Kahn topological sort to run *after* all declared dependencies have initialized).
     - `OnShutdown() noexcept`: Stop worker tasks and release subsystem resources (runs in strict *reverse* topological order).
     - `OnUnload() noexcept`: Unbind API table pointers and reset to fallback/null stubs.

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
