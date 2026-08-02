#pragma once

#include <Core/IDCore.hpp>
#include <IDLogger.hpp>

namespace ID
{
    extern ID_API std::shared_ptr<Logger> ID_Logger;
} // namespace ID

#define ID_TRACE(...)      ::ID::ID_Logger->trace(__VA_ARGS__)
#define ID_DEBUG(...)      ::ID::ID_Logger->debug(__VA_ARGS__)
#define ID_INFO(...)       ::ID::ID_Logger->info(__VA_ARGS__)
#define ID_WARN(...)       ::ID::ID_Logger->warn(__VA_ARGS__)
#define ID_ERROR(...)      ::ID::ID_Logger->error(__VA_ARGS__)


/**
 * @brief  条件为真时输出错误日志（Debug 模式下生效）
 *
 * 调用方负责执行 GL / 平台 API 调用，然后用条件表达式检查结果。
 * 若条件为 true，输出 `ID_ERROR` 级别的错误日志。
 *
 * @param condition  当值为 `true` 时触发错误输出
 * @param ...        传给 `ID_ERROR` 的格式化参数
 *
 * @note Release 模式下展开为空，**condition 不会被求值**，因此不得包含副作用。
 *
 * @par 示例
 * @code
 *      glTexImage2D(target, level, internal_format, width, height, 0, format, type, data);
 *      GLenum err = glGetError();
 *      ID_CHECK(err != GL_NO_ERROR, "glTexImage2D 失败: 0x{:x}", err);
 * @endcode
 */
#ifdef _ID_DEBUG
    #define ID_CHECK(condition, ...)                        \
        do                                                  \
        {                                                   \
            if(condition)                                   \
            {                                               \
                ::ID::ID_Logger->error(__VA_ARGS__);        \
            }                                               \
        } while(0)
#else
    #define ID_CHECK(condition, ...)
#endif 
         