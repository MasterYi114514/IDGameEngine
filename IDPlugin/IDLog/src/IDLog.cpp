#include "IDLog.hpp"

#include "IDLoggerManager.hpp"

/**
 *  导出函数是唯一的异常边界：底层日志库的失败以异常表达（例如文件打不开），
 *  这里逐个翻译为返回值/无效句柄，异常不跨 DLL 边界。
 */

namespace ID::Log
{
    void init()
    {
        try
        {
            LoggerManager::get_instance().initialize();
        }
        catch (...)
        {
            // 初始化失败时保持“未初始化”状态：后续 create_logger 返回无效句柄
        }
    }

    void shutdown()
    {
        try
        {
            LoggerManager::get_instance().shutdown();
        }
        catch (...)
        {
        }
    }

    LoggerHandle create_logger(const char* name)
    {
        try
        {
            return LoggerManager::get_instance().create(name);
        }
        catch (...)
        {
            return LoggerHandle::invalid_handle();
        }
    }

    Logger* get_logger(const LoggerHandle handle)
    {
        try
        {
            return LoggerManager::get_instance().get(handle);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void destroy_logger(const LoggerHandle handle)
    {
        try
        {
            LoggerManager::get_instance().destroy(handle);
        }
        catch (...)
        {
        }
    }
} // namespace ID::Log
