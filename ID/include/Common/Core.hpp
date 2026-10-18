#pragma once

// 函数调用宏定义

#if defined(_MSC_VER) || defined(__clang__) || defined(__GNUC__)

    // ---- 内联/冷路径 ----
    #if defined(_MSC_VER)
        #define ID_FORCEINLINE __forceinline
        #define ID_NOINLINE    __declspec(noinline)
        #define ID_COLD
    #else
        #define ID_FORCEINLINE inline __attribute__((always_inline))
        #define ID_NOINLINE    __attribute__((noinline))
        #define ID_COLD        __attribute__((cold))
    #endif

    // ---- restrict / trap ----
    #if defined(_MSC_VER)
        #define ID_RESTRICT __restrict
        #define ID_DEBUG_BREAK() __debugbreak()
    #else
        #define ID_RESTRICT __restrict__
        #define ID_DEBUG_BREAK() __builtin_trap()
    #endif

    // ---- 调用约定：只有 x86/x86_64 的 MSVC 与 Clang 才真正支持 ----
    #if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
        #define ID_VECTORCALL __vectorcall
        #define ID_VECTORCALL_SUPPORTED 1
    #else
        #define ID_VECTORCALL              // GCC / ARM64 上为空；ABI 关键处见守卫
    #endif

#else
    #error "IDGameEngine: unsupported compiler (expect MSVC, clang or GCC)"
#endif

// 符号导出、导入宏定义

#if defined(ID_STATIC_PLUGIN_LINK)
    #define ID_KERNEL_API
    #define ID_PLUGIN_API
#elif defined(_WIN32)
    #if defined(ID_KERNEL_BUILD_DLL)
        #define ID_KERNEL_API __declspec(dllexport)
    #else
        #define ID_KERNEL_API __declspec(dllimport)
    #endif
    #if defined(ID_PLUGIN_BUILD_DLL)
        #define ID_PLUGIN_API __declspec(dllexport)
    #else
        #define ID_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define ID_KERNEL_API __attribute__((visibility("default")))
    #define ID_PLUGIN_API __attribute__((visibility("default")))
#endif