#pragma once

#include "IDJsonCore.hpp"
#include "IDJsonParser.hpp"

/**
 *  IDJson 内部共享声明（私有头，不进公开 include 路径）。
 *
 *  只放"跨翻译单元但不对消费者暴露"的声明；实现细节留在各自 .cpp 的匿名 namespace 里。
 *  本文件与所有私有头都不包含 IDLog / spdlog / fmt：错误日志桥是唯一例外，
 *  它在 IDJsonLogBridge.cpp 内部包含 IDLog 头。
 */

namespace ID::Json::Detail
{
    /**
     *  @brief 记录一条解析失败诊断（错误日志桥的内部入口）
     *  @param error 失败原因与位置，由解析失败路径填好
     *  @note 只在解析失败冷路径调用；未 attach 日志器或 ID_JSON_ENABLE_LOG 为 0 时是空操作
     *  @note 定义在 IDJsonLogBridge.cpp：本插件唯一包含 IDLog 头的翻译单元
     */
    void log_parse_error(const JsonParseError& error) noexcept;
} // namespace ID::Json::Detail
