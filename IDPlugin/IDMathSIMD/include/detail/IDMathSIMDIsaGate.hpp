#pragma once

/**
 *  ID.Math.SIMD 的编译期 ISA 门（D0-02：本 backend 的基线 ISA = x64 SSE2）
 *  1. 不满足基线时**在预处理期直接 `#error`**：不生成任何代码、不留降级路径。这里没有
 *     `cpuid` 之类的运行期探测、没有函数指针表按 ISA 分派、没有标量 fallback——ISA 是编译期
 *     前提，不是运行期决策；
 *  2. 本头**不进入任何热路径**：它只参与一次条件编译，不产生符号、不产生分支、没有运行期代价；
 *  3. 判据按**编译器族**分支，不用 MinGW 的 MSVC 兼容宏：实测 mingw-w64 的 CRT 头在 x86_64 上
 *     会把 `_M_X64` / `_M_AMD64` 定义为 100，**即使配置了 `-mno-sse2`**。若拿这两个宏当 "SSE2 可用"
 *     的信号，门控会被头文件顺序静默绕过——那正是本门要防的事，因此这里只用编译器自身预定义的
 *     `_MSC_VER` / `__x86_64__` / `__SSE2__`；
 *  4. 本头**不做 AVX2/FMA 分派**：按 D0-02，AVX2/FMA 只是后续专用 variant 提案、本轮未冻结，
 *     因此这里既不要求、也不检测 `__AVX2__` / `__FMA__`，不为它预留宏开关（"假装存在"同样是违约）；
 *     用 `-mavx2 -mfma` 编译本 backend 是允许的——SSE2 是它们的子集，本 backend 只用 SSE2 基线语义；
 *  5. ISA flag 由构建层统一传播（`cmake/IDFlags.cmake` → `id_apply_performance_flags`），
 *     本插件不写 `target_compile_options` / `target_link_options` 重复 ISA 规则；
 *  6. 下游只应包含伞头 `IDMathSIMDApi.hpp`（它会先包含本头），不要单独依赖本头。
 */

// 基线之一：目标必须是 x86_64。32 位 x86（`_M_IX86` / `__i386__`）与其它架构一律拒绝：
// 那些目标上既没有本 backend 承诺的 16 字节对齐语义，也没有可用的 SSE2 基线。
#if defined(_MSC_VER)

    // MSVC（含 clang-cl）：x86_64 目标由 `_M_X64` / `_M_AMD64` 标识。
    #if !defined(_M_X64) && !defined(_M_AMD64)
        #error "ID.Math.SIMD 的基线 ISA 是 x64 SSE2：当前 MSVC 目标不是 x86_64，拒绝编译（不做运行期 ISA 调度，也不降级到标量路径）"
    #endif

#elif defined(__GNUC__) || defined(__clang__)

    // GCC / Clang（含 MinGW）：只看编译器自身的预定义宏。
    #if !defined(__x86_64__) && !defined(__amd64__)
        #error "ID.Math.SIMD 的基线 ISA 是 x64 SSE2：当前目标不是 x86_64，拒绝编译（不做运行期 ISA 调度，也不降级到标量路径）"
    #endif

#else

    #error "ID.Math.SIMD: 未适配的编译器（期望 MSVC / clang / GCC），无法确认 x64 SSE2 基线，拒绝编译"

#endif

// 基线之二：SSE2 必须可用。
//  - MSVC 的 x64 ABI 本身要求 SSE2 可用，且没有任何开关可以关闭它（`_M_X64` 已由上面的分支保证），
//    因此 MSVC 侧不需要额外判据；
//  - GCC / Clang 在 x86_64 上默认定义 `__SSE2__`，显式关闭（如 `-mno-sse2`）后不再定义——这条检查
//    因此能真实拦下"关掉 SSE2"的编译配置（已用 `g++ -mno-sse2` 实测取证）。
#if !defined(_MSC_VER) && !defined(__SSE2__)
    #error "ID.Math.SIMD 需要 SSE2：当前编译配置没有启用 SSE2（例如 -mno-sse2 或 32 位目标），拒绝编译（不做运行期 ISA 调度，也不降级到标量路径）"
#endif
