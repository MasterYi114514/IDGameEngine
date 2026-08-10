#pragma once

#ifdef IDPHYSICS_EXPORTS
    #ifdef _WIN32
        #define IDPHYSICS_API __declspec(dllexport)
    #else
        #define IDPHYSICS_API __attribute__((visibility("default")))
    #endif
#else
    #define IDPHYSICS_API
#endif
