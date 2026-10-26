#pragma once

/**
 *  IDLog 的导出宏与底层日志库（vendor 内 spdlog）编译期配置。
 *  
 *  本文件必须在任何 spdlog 头之前被包含：spdlog 的编译期开关只在这里集中设置一次。
 *  DLL 与每个消费者翻译单元必须使用同一套开关，否则同一批 inline 定义与类型会不一致（ODR 违约）。
 *  
 *  约定：
 *  - spdlog 以 header-only 方式使用：不定义 `SPDLOG_COMPILED_LIB`，不产生额外的静态库/导入库。
 *  - 不使用 spdlog 的默认 logger 与全局 registry：对象由本插件实例独占并显式释放。
 *  - 窄字符路径与默认命名空间：公开接口按 UTF-8 窄字符串传名与路径。
 *  - 包含 vendor 头会间接引入 Windows 头，因此在包含前收敛其范围并抑制 min/max 宏。
 */

// 顺序与命名空间探测，必须排在任何 spdlog 头之前：一旦 spdlog 被先包含，`SPDLOG_VER_MAJOR`
// （`spdlog/version.h`）或 `SPDLOG_NAMESPACE`（`spdlog/namespace.h`，被几乎所有 spdlog 头引入）
// 就已由 spdlog 自己定义。此时本文件的开关（header-only、关闭默认 logger、Windows 头守卫）
// 已经无法生效，同一批 inline 定义会以两套配置出现，构成 ODR 违约。
// 同理，用户自定义 `SPDLOG_NAMESPACE` 也会在这里被拒绝：本插件只支持默认外层命名空间。
#if defined(SPDLOG_VER_MAJOR) || defined(SPDLOG_NAMESPACE)
    #error "IDLogCore.hpp must be included before any spdlog header, and with the default spdlog namespace; spdlog was already included earlier or SPDLOG_NAMESPACE was overridden"
#endif

#include <spdlog/version.h>

// 包含后固定版本口径：本文件的开关与封装按 vendor 内 spdlog 1.x 编写，主版本不符即拒绝编译
#if SPDLOG_VER_MAJOR != 1
    #error "IDLog integrates the vendored spdlog 1.x; other major versions are not supported"
#endif

// 会改变 spdlog 类型布局、符号名或链接形态、且 spdlog 自身不会定义的开关：任一定义都会让本 DLL
// 与消费者翻译单元编译出不同的 spdlog 类型（ODR 违约），因此只允许本文件的取值（全部为默认值）。
// 注意 `SPDLOG_NAMESPACE` 不在此处：spdlog 会把它定义为默认值，只能放在上面的顺序探测里判。
#if defined(SPDLOG_COMPILED_LIB) || defined(SPDLOG_USE_STD_FORMAT) || defined(SPDLOG_WCHAR_FILENAMES) || \
    defined(SPDLOG_NO_ATOMIC_LEVELS) || defined(SPDLOG_FMT_EXTERNAL) || defined(SPDLOG_FMT_EXTERNAL_HO)
    #error "IDLog integrates spdlog in header-only mode with bundled fmt and narrow-char paths; do not define SPDLOG_COMPILED_LIB / SPDLOG_USE_STD_FORMAT / SPDLOG_WCHAR_FILENAMES / SPDLOG_NO_ATOMIC_LEVELS / SPDLOG_FMT_EXTERNAL / SPDLOG_FMT_EXTERNAL_HO"
#endif

#if !defined(SPDLOG_DISABLE_DEFAULT_LOGGER)
    #define SPDLOG_DISABLE_DEFAULT_LOGGER
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
#endif

#if !defined(NOMINMAX)
    #define NOMINMAX
#endif

// 导出宏：DLL 内以 ID_LOG_EXPORTS 编译；Windows 下 MSVC 与 MinGW GCC 都接受 __declspec
#if defined(_WIN32)
    #if defined(ID_LOG_EXPORTS)
        #define ID_LOG_API __declspec(dllexport)
    #else
        #define ID_LOG_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(ID_LOG_EXPORTS)
        #define ID_LOG_API __attribute__((visibility("default")))
    #else
        #define ID_LOG_API
    #endif
#else
    #define ID_LOG_API
#endif
