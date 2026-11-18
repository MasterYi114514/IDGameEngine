#pragma once

#include "IDLog.hpp"

/**
 *  项目模块的日志器载体：模块日志器变量、六个级别宏与日志断言宏。
 *
 *  本头是模块私有头，不进任何公开 include；需要打日志的 .cpp 用相对 include 根的稳定路径
 *  `"project/Log/Log.hpp"` 包含它。
 *
 *  不变量：
 *  1. 日志器是单实例：`extern` 声明 + `Log.cpp` 的唯一定义；不写 `inline`、不写 `static`、
 *     不放匿名命名空间、不在每个翻译单元各定义一份。这是 cpp-style 第 5、6 节关于全局状态与
 *     命名规则的显式例外，理由与边界见 skills/log-style 第 2.5 节。
 *  2. 指针只被 `LifeCycle::OnInit::IDProject_logger_init()` 写入一次，就绪后不再改写；业务代码
 *     只经下面的宏使用它，不直接调用 `::ID::Log::get_logger` 或日志器对象。
 *  3. 六个级别宏各是单个表达式，原样转发格式串与参数；宏体内不判空、不做惰性初始化、不捕获异常。
 *     就绪与否由 `IDProject_logger_init()` 的返回值表达，不在每个调用点重复检查。
 */

namespace ID::Project
{
    extern ::ID::Log::Logger* IDProject_logger;
} // namespace ID::Project

// 六个级别各一个宏，一一对应日志器的六个级别方法；fatal 是唯一的终止级方法
#define IDPROJECT_LOG_TRACE(...)   ::ID::Project::IDProject_logger->trace(__VA_ARGS__)
#define IDPROJECT_LOG_DEBUG(...)   ::ID::Project::IDProject_logger->debug(__VA_ARGS__)
#define IDPROJECT_LOG_INFO(...)    ::ID::Project::IDProject_logger->info(__VA_ARGS__)
#define IDPROJECT_LOG_WARN(...)    ::ID::Project::IDProject_logger->warn(__VA_ARGS__)
#define IDPROJECT_LOG_ERROR(...)   ::ID::Project::IDProject_logger->error(__VA_ARGS__)
#define IDPROJECT_LOG_FATAL(...)   ::ID::Project::IDProject_logger->fatal(__VA_ARGS__)

// 断言宏：debug 分支记一条 error 后继续执行（不 abort、不改变控制流）；非 debug 展开为空，
// 条件与参数都不求值。条件必须是纯表达式，不得含副作用
#ifdef IDPROJECT_DEBUG
    #define IDPROJECT_LOG_ASSERT(cond, ...)                                                        \
        do                                                                                         \
        {                                                                                          \
            if (!(cond))                                                                           \
            {                                                                                      \
                ::ID::Project::IDProject_logger->error(__VA_ARGS__);                                \
            }                                                                                      \
        } while (false)
#else
    #define IDPROJECT_LOG_ASSERT(cond, ...)
#endif
