#pragma once

#ifdef _WIN32
    #ifdef IDLOG_EXPORTS
        #define IDLOG_API __declspec(dllexport)
    #else
        #define IDLOG_API __declspec(dllimport)
    #endif
#else
    #define IDLOG_API
#endif