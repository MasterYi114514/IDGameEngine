#pragma once

#ifdef IDASSET_EXPORTS
    #ifdef _WIN32
        #define IDASSET_API __declspec(dllexport)
    #else
        #define IDASSET_API __attribute__((visibility("default")))
    #endif
#else
    #define IDASSET_API
#endif