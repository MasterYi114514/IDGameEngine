#pragma once

/**
 *  公开头自包含探针的依赖守卫。
 *
 *  用法：`src/window/headers/probe_*.cpp` 每个 TU **只包含一个** IDWindow 公开头，随后立即包含本头。
 *  `#pragma once` 之外本头不含任何 include，因此"编译通过"同时证明两件事：
 *
 *  1. 该公开头单独包含即可编译（自包含），不依赖其它公开头的先包含顺序；
 *  2. 该公开头没有把 GLFW / 平台（`windows.h`）/ Vulkan / IDLog 的头带进消费者——
 *     只要带进来，对应宏就会已定义，下面的 `#error` 在构建期失败。
 *
 *  依据（契约）：README 与全部公开头都写明"公开头不含 GLFW / 平台 / Vulkan / IDLog 头，也不出现
 *  任何平台类型名"。判定用各三方的**包含守卫或导出宏**，它们是三方头引入后必然出现的符号：
 *  - GLFW：`GLFW_VERSION_MAJOR` / `GLFW_VERSION_MINOR`（`GLFW/glfw3.h`）
 *  - 平台：`_WINDOWS_`（`windows.h` 的包含守卫）
 *  - Vulkan：`VULKAN_H_` / `VULKAN_CORE_H_`（`vulkan/vulkan.h` 与 `vulkan_core.h` 的守卫）
 *  - IDLog：`ID_LOG_API`（`IDLogCore.hpp` 在所有分支都会定义它）
 */

#if defined(GLFW_VERSION_MAJOR) || defined(GLFW_VERSION_MINOR) || defined(_GLFW_H_)
    #error "IDWindow 公开头把 GLFW 头带进了消费者：契约要求公开头不含任何 GLFW 头"
#endif

#if defined(_WINDOWS_) || defined(_WINDOWS_H) || defined(WIN32_LEAN_AND_MEAN)
    #error "IDWindow 公开头把 windows.h（或平台宏）带进了消费者：契约要求公开头不出现任何平台类型名"
#endif

#if defined(VULKAN_H_) || defined(VULKAN_CORE_H_)
    #error "IDWindow 公开头把 Vulkan 头带进了消费者：契约要求公开头不含任何 Vulkan 头"
#endif

#if defined(ID_LOG_API) || defined(IDLOG_VERSION_MAJOR)
    #error "IDWindow 公开头把 IDLog 头带进了消费者：日志只决定诊断通道是否可用，不是公开头的依赖"
#endif
