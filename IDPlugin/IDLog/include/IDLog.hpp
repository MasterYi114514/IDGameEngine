#pragma once

#include "IDHandle.hpp"
#include "IDLogCore.hpp"
#include "IDLogger.hpp"

/**
 *  日志插件的对外入口（DLL 导出）。
 *  
 *  就绪条件：
 *  - 消费者必须先调用 `init()`；未初始化时 `create_logger` 返回无效句柄，不隐式初始化。
 *  - `init()` 幂等；`shutdown()` 之后可以再次 `init()`。
 *  
 *  句柄与指针的生命周期：
 *  - `LoggerHandle` 与 `Logger*` 都只属于当前那一轮 `init` / `shutdown` 生命周期。`shutdown()` 之后
 *    重新 `init()`，槽位下标从 0 重新分配，旧句柄的数值可能被新日志器复用（ABA）：此时
 *    `get_logger(旧句柄)` 会返回**新日志器**而不是 nullptr，调用方无法据返回值分辨。
 *    因此重新 `init()` 之后必须重新 `create_logger`，不得复用上一轮的句柄；本插件不提供代数/版本位。
 *  
 *  线程契约：
 *  - `init` / `shutdown` / `create_logger` / `get_logger` / `destroy_logger` 之间由本插件互斥保护，
 *    可由多线程调用；`init` 与 `shutdown` 本身不得并发（生命周期由产品侧串行决定）。
 *  - 日志写入不经登记锁，见 `Logger` 的线程契约。
 *  
 *  失败语义：
 *  - 可恢复失败一律以无效句柄 / nullptr 表达，异常不跨 DLL 边界。
 *  - 文件打不开不会导致进程终止，只在 `Logger::set_sink_path` 返回 false。
 *  - 已启用目标在运行期的写失败／格式化失败不抛出、不终止：该条消息被丢弃，并由底层日志库
 *    向 stderr 限频报告（约每秒 1 条）；调用方没有逐条返回值可观测。详见 `Logger` 的失败语义。
 *  - `shutdown()` 结束全部槽位对象的生命周期：此后经旧 `Logger*` 调用（含日志方法与配置方法）
 *    属未定义行为，本插件不承诺安全，也不承诺不崩溃。`destroy_logger` 不同——槽位对象仍留在池内
 *    并被清空，残留在该指针上的日志调用不产生输出、配置方法返回 false（实现只保证不崩溃），但
 *    槽位可能被后续 `create_logger` 复用，届时指针指向新日志器，因此同样不得继续使用。
 *  
 *  成本边界：
 *  - 登记类入口是冷路径，可能分配、可能加锁；日志写入的裁剪与格式化成本见 `Logger`。
 *  - 具体读数与评级见 `README.md` 的验证状态节与正式报告，本注释不转抄数字。
 */

namespace ID::Log
{
    using LoggerHandle = ::ID::Common::Handle<std::uint32_t, Logger>;

    // ========== 生命周期函数 ==========

    /**
     *  @brief 初始化日志插件
     *  @note 幂等；无失败返回，内部异常被吞掉并保持“未初始化”状态
     *  @note 与 `shutdown` 不得并发调用
     */
    ID_LOG_API void init();

    /**
     *  @brief 关闭日志插件：flush 全部存活 logger，销毁残留条目并复位状态
     *  @note 文件目标先 flush 再关闭，不承诺掉电级持久（不做 fsync）
     *  @note 未初始化时是无操作；之后可以再次 `init()`
     *  @note 使所有 `LoggerHandle` 与 `Logger*` 失效；此后经旧 `Logger*` 的任何调用（含日志方法与
     *        配置方法）属未定义行为，不承诺安全，也不承诺不崩溃。需要继续写日志时先重新 `init()`
     *        并重新 `create_logger`
     *  @note 句柄不跨生命周期：再次 `init()` 后槽位下标从 0 重新分配，旧 `LoggerHandle` 的数值可能
     *        被新日志器复用（ABA），`get_logger` 届时返回的是新日志器而非 nullptr；调用方必须重新
     *        `create_logger`，不得复用旧句柄
     */
    ID_LOG_API void shutdown();

    // ========== 日志功能 ==========

    /**
     *  @brief 创建日志器
     *  @param name 日志器名（模式串 %n），UTF-8；可为 nullptr，按空名处理
     *  @return 有效句柄；未初始化、内存不足或后端创建失败时返回无效句柄（`is_valid()` 为 false）
     *  @note 默认配置：终端目标启用（阈值 Trace），文件目标停用（需 `Logger::set_sink_path` 提供路径）
     *  @note 登记类操作，内部加锁；可能分配
     *  @note 后端构造在锁外进行，插入前会在锁内重新确认仍处于已初始化状态：若构造期间另一线程完成了
     *        `shutdown()`，本次调用返回无效句柄，不会把日志器登记进一个已关闭的登记表
     */
    ID_LOG_API LoggerHandle create_logger(const char* name);

    /**
     *  @brief 按句柄取日志器
     *  @param handle 由 `create_logger` 返回的句柄
     *  @return 句柄有效且槽位存活时返回对象指针；否则返回 nullptr
     *  @note 返回的指针只保证到下一次 `create_logger` / `destroy_logger` / `shutdown` 之前有效
     *        （槽位数组扩容会使旧指针失效）。需要长期持有时保存 `LoggerHandle`，每次使用前重新取。
     *  @note 句柄只属于当前生命周期：`shutdown` 之后重新 `init`，旧句柄的数值可能被新日志器复用，
     *        此时本函数返回新日志器而不是 nullptr，调用方无法区分（ABA）；不得靠本函数的返回值
     *        判断“旧句柄是否还有效”
     *  @note 登记类操作，内部加锁；不分配
     */
    ID_LOG_API Logger* get_logger(const LoggerHandle handle);

    /**
     *  @brief 销毁日志器：flush 后释放其 logger 与 sink，句柄随即失效
     *  @param handle 由 `create_logger` 返回的句柄
     *  @note 无效句柄或已销毁的句柄是无操作；对同一句柄重复调用不会重复回收槽位
     *  @note 销毁后该句柄指向的槽位可能被后续 `create_logger` 复用，陈旧句柄读到的是新日志器
     *        （契约要求调用方不再使用已销毁的句柄）
     *  @note 登记类操作，内部加锁
     */
    ID_LOG_API void destroy_logger(const LoggerHandle handle);
} // namespace ID::Log
