#pragma once

#ifdef _WIN32
    #ifdef IDMATH_STATIC
        #define IDMATH_API
    #elif defined(IDMATH_EXPORTS)
        #define IDMATH_API __declspec(dllexport)
    #else
        #define IDMATH_API __declspec(dllimport)
    #endif
#else
    #define IDMATH_API
#endif