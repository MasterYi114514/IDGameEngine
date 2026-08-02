#pragma once

#ifdef _WIN32
    #ifdef IDRENDERER_EXPORTS
        #define IDR_API __declspec(dllexport)
    #else
        #define IDR_API __declspec(dllimport)
    #endif
#else
    #define IDR_API
#endif