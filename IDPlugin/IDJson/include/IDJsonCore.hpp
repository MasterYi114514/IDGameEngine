#pragma once

/**
 *  IDJson 的导出宏与编译期开关。
 *
 *  本文件只放"跨模块可见的编译期配置"，不含任何类型或函数声明；IDJson 的公开头都先包含它。
 *
 *  交付形态与导出宏：
 *  - 当前以 STATIC 交付，构建清单对目标设置 `ID_JSON_STATIC`，宏展开为空（静态库不需要导入/导出声明）；
 *  - 若将来改为 SHARED，改为在构建清单里只为库自身定义 `ID_JSON_EXPORTS` 即可，下面的
 *    `dllexport`/`dllimport`（Windows）与 `visibility("default")`（GCC/Clang）分支已经就位，
 *    消费者侧无需改代码。
 *
 *  日志开关 `ID_JSON_ENABLE_LOG`：
 *  - 默认 1（ON）。它由构建清单以 PRIVATE 方式只定义在 IDJson 自己的翻译单元上，**不改变任何公开
 *    声明**，因此库与消费者之间不存在 ODR 差异：`IDLog` / `spdlog` / `fmt` 的头不进 IDJson 的
 *    公开 include 链（公开头只前向声明 `ID::Log::Logger`）。
 *  - 置 0（OFF）时 `attach_error_logger` / `detach_error_logger` 与解析失败的错误输出路径都编译为
 *    空操作：不包含任何 IDLog 头、不链接 IDLog、不产生日志；解析与序列化行为完全不变。
 *  - 两个取值都必须显式定义（`ID_JSON_ENABLE_LOG=1` 或 `=0`）：本文件只在未定义时补默认值，
 *    以免出现"`-DID_JSON_ENABLE_LOG` 无值"这种 `#if` 无法求值的形式。
 */

#if !defined(ID_JSON_ENABLE_LOG)
    #define ID_JSON_ENABLE_LOG 1
#endif

// 导出宏：静态库形态下为空
#if defined(ID_JSON_STATIC)
    #define ID_JSON_API
#elif defined(_WIN32)
    #if defined(ID_JSON_EXPORTS)
        #define ID_JSON_API __declspec(dllexport)
    #else
        #define ID_JSON_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(ID_JSON_EXPORTS)
        #define ID_JSON_API __attribute__((visibility("default")))
    #else
        #define ID_JSON_API
    #endif
#else
    #define ID_JSON_API
#endif
