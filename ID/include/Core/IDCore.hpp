#pragma once

#ifdef _WIN32
    #ifdef _ID_SHARED_LIB
        #ifdef ID_EXPORT
            #define ID_API __declspec(dllexport)
        #else
            #define ID_API __declspec(dllimport)
        #endif
    #else
        #define ID_API
    #endif
#else
    #define ID_API
#endif