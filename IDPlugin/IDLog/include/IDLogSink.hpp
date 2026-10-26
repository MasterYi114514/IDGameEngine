#pragma once

#include <cstdint>

/**
 *  输出目标类型。
 *  
 *  取值只用于运行期分派：同一个 `Logger` 在每种类型上最多持有一个目标对象，
 *  启用/停用只改变该目标是否挂载到后端（终端目标在首次启用时创建），目标对象与其阈值保留以便再次复用。
 *  
 *  本期只实现 Console 与 File 两种。其它目标（例如网络）在真正实现时才增加取值，
 *  不预先占位：避免出现"枚举存在但不可用"的死枚举，也不让消费者写出以为可用的配置。
 *  
 *  非法取值：把超出上述取值的整数强制转换为 `SinkType` 后传入时，四个配置方法
 *  （`enable_sink` / `disable_sink` / `set_sink_level` / `set_sink_path`）一律返回 `false`，
 *  不会静默当作 `Console` 或 `File` 处理，也不改动任何已挂载目标与阈值。
 *  `Level` 参数同策略：`set_sink_level` 对超出 `Level::Fatal` 的强制转换值返回 `false`
 *  且不改动任何阈值，见 `IDLogger.hpp`。
 */

namespace ID::Log
{
    enum class SinkType : uint8_t
    {
        Console = 0,    // 终端（彩色）输出；创建日志器时默认启用
        File    = 1     // 文件输出；路径由 `Logger::set_sink_path` 提供，默认停用
    };
} // namespace ID::Log
