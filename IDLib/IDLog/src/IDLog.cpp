#include "IDLog.hpp"
#include "IDLogger.hpp"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <memory>
#include <mutex>
#include <vector>

namespace ID
{
    // 存储所有 Logger 实例的静态成员变量，生命周期与程序相同
    std::unordered_map<std::string, std::shared_ptr<Logger>> Logger::loggers = {};
    
    namespace
    {
        // 存储所有 spdlog::logger 实例的静态成员变量，生命周期与程序相同
        std::unordered_map<std::string, std::unique_ptr<spdlog::logger>> spdloggers = {};

        // 全局 sink 回调（DevGUI ConsolePanel 等），带互斥锁保证线程安全
        std::mutex g_sink_mutex;
        Log::LogSinkCallback g_sink_callback;
        
        /*
            获取名称为 name 的 spdlog::logger 实例
            如果没有找到对应名称的 Logger，则返回默认 Logger 的实例
        */
        std::unique_ptr<spdlog::logger>& get_spdlogger(const std::string& name)
        {
            if(spdloggers.find(name) == spdloggers.end())
            {
                return spdloggers[default_logger->get_name()];
            }

            return spdloggers[name];
        }
    } // namespace

    // ID::Logger ------------------------------------------------------------------------------------
    Logger::Logger(const std::string& name) : name(name) { }

    Logger::~Logger() = default;

    void Logger::set_level(Log::Level level)
    {
        auto &logger_uptr = get_spdlogger(name);
        if(logger_uptr == nullptr)
        {
            default_logger->error("get_spdlogger 在获取名叫 '{}' 的 spdlog logger 时返回了 nullptr", name);
            return;
        }

        switch(level)
        {
            case Log::Level::Trace:
                logger_uptr->set_level(spdlog::level::trace);
                break;
            case Log::Level::Debug:
                logger_uptr->set_level(spdlog::level::debug);
                break;
            case Log::Level::Info:
                logger_uptr->set_level(spdlog::level::info);
                break;
            case Log::Level::Warning:
                logger_uptr->set_level(spdlog::level::warn);
                break;
            case Log::Level::Error:
                logger_uptr->set_level(spdlog::level::err);
                break;
        }
    }

    void Logger::set_name(const std::string& new_name)
    {
        // 获取当前 spdlog::logger 实例
        auto &logger_uptr = get_spdlogger(name);
        if(logger_uptr == nullptr)
        {
            default_logger->error("get_spdlogger 在获取名叫 '{}' 的 spdlog logger 时返回了 nullptr", name);
            return;
        }

        // todo 修改 spdlog::logger 实例的名称

        // 从 spdloggers 中删除旧名称的记录，并添加新名称的记录
        spdloggers.erase(name);
        spdloggers[new_name] = std::move(logger_uptr);

        // 修改 Logger 实例的名称
        name = new_name;
    }

    const std::string& Logger::get_name() const
    {
        return name;
    }

    void Logger::write(Log::Level level, const std::string& message)
    {
        // 先通知全局 sink 回调（如 DevGUI ConsolePanel）
        {
            std::lock_guard<std::mutex> lock(g_sink_mutex);
            if(g_sink_callback)
            {
                g_sink_callback(level, message);
            }
        }

        auto &logger_uptr = get_spdlogger(name);
        if(logger_uptr == nullptr)
        {
            default_logger->error("get_spdlogger 在获取名叫 '{}' 的 spdlog logger 时返回了 nullptr", name);
        }

        switch(level)
        {
            case Log::Level::Trace:
                logger_uptr->trace(message);
                break;
            case Log::Level::Debug:
                logger_uptr->debug(message);
                break;
            case Log::Level::Info:
                logger_uptr->info(message);
                break;
            case Log::Level::Warning:
                logger_uptr->warn(message);
                break;
            case Log::Level::Error:
                logger_uptr->error(message);
                break;
        }
    }

    // ID::Log ---------------------------------------------------------------------------------------
    namespace Log
    {
        std::shared_ptr<Logger> create_logger(const std::string& name, Log::Level level)
        {
            // 如果已经存在一个名称为 name 的 Logger 实例，则返回该实例的智能指针
            // 并调用默认日志进行报错
            if(Logger::loggers.find(name) != Logger::loggers.end())
            {
                default_logger->warn("尝试用 create_logger 方法再次创建名叫 {} 的 Logger，已存在同名 Logger 将被返回", name);
                return Logger::loggers[name];
            }

            if(spdloggers.find(name) != spdloggers.end())
            {
                default_logger->error("名叫 '{}' 的 Logger 不存在但是其对应的 spdlog Logger 已经存在", name);
                return nullptr;
            }

            // 创建一个新的 Logger 实例，并将其存储在 loggers 中
            Logger* instance = new Logger(name);
            std::shared_ptr<Logger> sptr_instance(instance);
            Logger::loggers[name] = sptr_instance;

            // 创建一个新的 spdlog::logger 实例，并将其存储在 spdloggers 中
            auto logger_ptr = spdlog::stdout_color_mt(name);
            logger_ptr->set_pattern("%^[%T %n] %v%$");
            spdloggers[name] = std::make_unique<spdlog::logger>(std::move(*logger_ptr));

            // 设置日志级别
            instance->set_level(level);

            if(name != "ID Logger管理器")
            {
                default_logger->info("成功创建名叫 '{}' 的 Logger", name);
            }

            return sptr_instance;
        }
        
        std::shared_ptr<Logger> get_logger(const std::string& name)
        {
            // 如果没有找到对应名称的 Logger，则返回一个空指针
            if(Logger::loggers.find(name) == Logger::loggers.end())
            {
                default_logger->error("名叫 '{}' 的 Logger 不存在", name);
                return nullptr;
            }

            return Logger::loggers[name];
        }

        void destroy_logger(const std::string& name)
        {
            if(name == "ID Logger管理器")
            {
                default_logger->error("无法销毁默认 Logger 'ID Logger管理器'");
                return;
            }            // 从 loggers 中删除名为 name 的 Logger 记录
            if(Logger::loggers.erase(name) == 0)
            {
                default_logger->error("名叫 '{}' 的 Logger 不存在，无法销毁", name);
            }

            // 从 spdloggers 中删除名为 name 的 spdlog::logger 记录
            if(spdloggers.erase(name) == 0)
            {
                default_logger->error("名叫 '{}' 的 spdlog Logger 不存在，无法销毁", name);
            }

            default_logger->info("成功销毁名叫 '{}' 的 Logger", name);
        }

        void set_sink(LogSinkCallback callback)
        {
            std::lock_guard<std::mutex> lock(g_sink_mutex);
            g_sink_callback = std::move(callback);
        }

    } // namespace Log

} // namespace ID

// 在单一翻译单元中定义并初始化默认日志器，避免头内静态初始化顺序问题
namespace ID
{
    std::shared_ptr<Logger> default_logger = Log::create_logger("ID Logger管理器");
}