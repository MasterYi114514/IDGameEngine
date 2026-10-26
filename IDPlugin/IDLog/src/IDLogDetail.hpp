#pragma once

#include <cstdint>
#include <string_view>

#include <spdlog/common.h>

#include "Core/Core.hpp"
#include "IDLoggerLevel.hpp"

/**
 *  IDLog 内部共用的小工具：级别映射与日志模式串。
 *  只用于 `src/` 内的翻译单元，不进入公开契约，也不出现在公开 include 路径的 API 上。
 */

namespace ID::Log::Detail
{
    // 终端与文件使用同一模式串；文件 sink 忽略 `%^` / `%$` 的色彩标记
    inline constexpr std::string_view kPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";

    /**
     *  @brief 运行期校验 `Level` 取值是否落在枚举范围内（`Trace` 到 `Fatal`）
     *  @note 六个固定日志方法只用编译期常量作级别，不需要本校验；配置入口接收的是运行期参数，
     *        强制转换出的越界值必须在写入阈值之前被拒绝，不得落到映射的默认分支被当成某个级别
     */
    ID_FORCEINLINE constexpr bool is_valid_level(Level level) noexcept
    {
        return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(Level::Fatal);
    }

    /**
     *  @brief IDLog 级别到 spdlog 级别的映射
     *  @note Fatal 映射为 critical：本插件不提供高于 critical 的阈值，因此 fatal 不会被裁剪
     *  @note 调用方必须先用 `is_valid_level` 校验：越界值会落到下面的默认分支（`trace`），
     *        那条分支只为满足编译器的穷尽性返回而存在，不是可接受的降级目标
     */
    ID_FORCEINLINE constexpr spdlog::level::level_enum to_spdlog_level(Level level) noexcept
    {
        switch (level)
        {
            case Level::Trace:  return spdlog::level::trace;
            case Level::Debug:  return spdlog::level::debug;
            case Level::Info:   return spdlog::level::info;
            case Level::Warn:   return spdlog::level::warn;
            case Level::Error:  return spdlog::level::err;
            case Level::Fatal:  return spdlog::level::critical;
        }

        return spdlog::level::trace;
    }
} // namespace ID::Log::Detail
