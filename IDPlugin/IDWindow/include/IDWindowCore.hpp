#pragma once

/**
 *  IDWindow 的版本常量与导出宏。
 *
 *  本文件只放"跨模块可见的编译期配置"，不含任何 `#include`、不承载任何类型或函数声明；
 *  需要导出宏 `ID_WINDOW_API` 的公开头各自包含它。`ID_FORCEINLINE` 不来自本文件：
 *  它定义在 `Common/Core/Core.hpp`，需要它的头直接包含 `Core/Core.hpp`。
 *
 *  交付形态与导出宏：
 *  - 默认 STATIC：构建清单对目标设置 `ID_WINDOW_STATIC`，宏展开为空（静态库不需要导入/导出声明）。
 *  - `IDWINDOW_BUILD_SHARED=ON` 时构建清单只为库自身定义 `ID_WINDOW_EXPORTS`；消费者不定义任何
 *    宏，公开头按 `dllimport`（Windows）或空（GCC/Clang）声明，与 `IDJson` / `IDLog` 同款三段式。
 *
 *  版本口径：`ID_WINDOW_VERSION_MAJOR` / `ID_WINDOW_VERSION_MINOR` 是本插件的发布号，
 *  `ID_WINDOW_INTERFACE_VERSION` 是契约兼容性号；破坏性契约变更必须升接口版本。
 *  接口版本 2 相对 1 的破坏性变更：命名空间整体收进 `ID::Window`、`WindowStatus` 与几何 POD
 *  落地、`WindowPool` 公开面冻结、事件层只剩初始化与开关、`Window` 只由 `WindowPool` 持有。
 */

#define ID_WINDOW_VERSION_MAJOR     0
#define ID_WINDOW_VERSION_MINOR     1
#define ID_WINDOW_INTERFACE_VERSION 2

// 导出宏：静态库形态下为空
#if defined(ID_WINDOW_STATIC)
    #define ID_WINDOW_API
#elif defined(_WIN32)
    #if defined(ID_WINDOW_EXPORTS)
        #define ID_WINDOW_API __declspec(dllexport)
    #else
        #define ID_WINDOW_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(ID_WINDOW_EXPORTS)
        #define ID_WINDOW_API __attribute__((visibility("default")))
    #else
        #define ID_WINDOW_API
    #endif
#else
    #define ID_WINDOW_API
#endif
