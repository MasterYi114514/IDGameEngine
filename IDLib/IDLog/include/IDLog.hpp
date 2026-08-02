#pragma once

#include <format>
#include <string_view>
#include <utility>
#include <memory>

#include "IDLogCore.hpp"

namespace ID
{
    class Logger;

    namespace Log
    {
        enum class Level
        {
            Trace,
            Debug,
            Info,
            Warning,
            Error,
        };

        /*
            创建一个新的 Logger 实例，并且使用传入的名称
            返回一个指向新 Logger 的共享指针
            如果该名字已存在则返回空指针
        */
        IDLOG_API std::shared_ptr<Logger> create_logger(const std::string& name, Log::Level level = Log::Level::Info);

        /*        
            根据名称获取一个已存在的 Logger 实例
            返回一个指向该 Logger 的共享指针
            如果没有找到对应名称的 Logger，则返回一个空指针
        */
        IDLOG_API std::shared_ptr<Logger> get_logger(const std::string& name);
        
        /*
            清除 Logger::loggers 中名为 name 的 Logger 记录
            想要彻底销毁该 Logger 实例，还需要在外部将指向该 Logger 的所有共享指针置空，以便触发析构函数
        */
        IDLOG_API void destroy_logger(const std::string& name);

    } // namespace Log

} // namespace ID