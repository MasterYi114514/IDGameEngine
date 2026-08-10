#pragma once

#ifdef _WIN32
    #ifdef IDAUDIO_EXPORTS
        #define IDAUDIO_API __declspec(dllexport)
    #else
        #define IDAUDIO_API __declspec(dllimport)
    #endif
#else
    #ifdef IDAUDIO_EXPORTS
        #define IDAUDIO_API __attribute__((visibility("default")))
    #else
        #define IDAUDIO_API
    #endif
#endif
