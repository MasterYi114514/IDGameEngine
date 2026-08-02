#pragma once

#include <string>
#include <format>

/*
    在头文件中使用日志记录器的支持
    本质上是对 src 里的 IDLogger 的进一步封装
*/
namespace ID::HeaderLogger
{
    void write(const std::string& message);
    
    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        write(std::format(fmt, std::forward<Args>(args)...));
    }
}

#define HEAD_ERROR(...)      ::ID::HeaderLogger::error(__VA_ARGS__)