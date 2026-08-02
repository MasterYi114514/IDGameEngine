#pragma once

#ifdef _WIN32
    #ifdef IDWINDOW_EXPORTS
        #define IDWINDOW_API __declspec(dllexport)
    #else
        #define IDWINDOW_API __declspec(dllimport)
    #endif
#else
    #define IDWINDOW_API
#endif