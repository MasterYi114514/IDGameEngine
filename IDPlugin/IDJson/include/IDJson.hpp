#pragma once

/**
 *  IDJson 的伞头：一次包含全部公开契约。
 *
 *  只需要部分能力时可以只包含对应头，避免把解析器/序列化器的声明一起带进来：
 *  - `IDJsonValue.hpp`    ：JsonValue / JsonType 与内联访问器
 *  - `IDJsonDocument.hpp` ：JsonDocument（arena 与根值）
 *  - `IDJsonParser.hpp`   ：parse / parse_non_owning、错误码与错误日志桥
 *  - `IDJsonWriter.hpp`   ：serialize 与缩进选项
 *
 *  @note 本伞头不包含任何 IDLog / spdlog / fmt 头：错误日志桥只用到 `ID::Log::Logger` 的前向声明。
 */

#include "IDJsonCore.hpp"
#include "IDJsonValue.hpp"
#include "IDJsonDocument.hpp"
#include "IDJsonParser.hpp"
#include "IDJsonWriter.hpp"
