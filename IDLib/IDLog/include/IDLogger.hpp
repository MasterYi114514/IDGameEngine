#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "IDLog.hpp"

namespace ID
{
    class IDLOG_API Logger
    { 
        // 友元方法，用于在外部创建名称为 name 的 Logger 实例
        friend IDLOG_API std::shared_ptr<Logger> Log::create_logger(const std::string& name, Log::Level level);

        // 友元方法，用于在外部获取名称为 name 的 Logger 实例
        friend IDLOG_API std::shared_ptr<Logger> Log::get_logger(const std::string& name);

        // 友元方法，用于在外部删除 loggers 对名为 name 的 Logger 的记录
        friend IDLOG_API void Log::destroy_logger(const std::string& name);

    private:
        // 私有构造函数，确保只能被友元方法创建实例
        Logger(const std::string& name);

    public:
        // 公开析构函数，用于智能指针调用
        ~Logger();

    public:
        // 供外部调用的方法
        
        // void init();

        // 设置日志的显示级别
        void set_level(Log::Level level);

        // 设置 Logger 的名称
        void set_name(const std::string& new_name);

        // 设置 Logger 的格式
        // void set_pattern(const std::string& pattern);

        // 以只读方式获取日志器名称
        const std::string& get_name() const;
        template<typename... Args>
        void trace(std::format_string<Args...> fmt, Args&&... args);

        template<typename... Args>
        void debug(std::format_string<Args...> fmt, Args&&... args);

        template<typename... Args>
        void info(std::format_string<Args...> fmt, Args&&... args);

        template<typename... Args>
        void warn(std::format_string<Args...> fmt, Args&&... args);

        template<typename... Args>
        void error(std::format_string<Args...> fmt, Args&&... args);

        void write(Log::Level level, const std::string& message);
    private:
        // 成员变量
        std::string name;           // 当前日志器的名称
        static std::unordered_map<std::string, std::shared_ptr<Logger>> loggers;    // 存储所有日志器实例的静态成员变量
    };

    template<typename... Args>
    inline void Logger::trace(std::format_string<Args...> fmt, Args&&... args)
    {
        write(Log::Level::Trace, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void Logger::debug(std::format_string<Args...> fmt, Args&&... args)
    {
        write(Log::Level::Debug, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void Logger::info(std::format_string<Args...> fmt, Args&&... args)
    {
        write(Log::Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void Logger::warn(std::format_string<Args...> fmt, Args&&... args)
    {
        write(Log::Level::Warning, std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void Logger::error(std::format_string<Args...> fmt, Args&&... args)
    {
        write(Log::Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    IDLOG_API extern std::shared_ptr<Logger> default_logger;
} // namespace ID