#pragma once

#if defined(_MSC_VER)
    #define ID_FORCEINLINE __forceinline
    #define ID_NOINLINE    __declspec(noinline)
    #define ID_VECTORCALL  __vectorcall
    #define ID_RESTRICT    __restrict
    #define ID_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define ID_FORCEINLINE inline __attribute__((always_inline))
    #define ID_NOINLINE    __attribute__((noinline, cold))
    #define ID_VECTORCALL  __vectorcall
    #define ID_RESTRICT    __restrict__
    #define ID_DEBUG_BREAK() __builtin_trap()
#else
    #define ID_FORCEINLINE inline
    #define ID_NOINLINE
    #define ID_VECTORCALL
    #define ID_RESTRICT
    #define ID_DEBUG_BREAK()
#endif

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