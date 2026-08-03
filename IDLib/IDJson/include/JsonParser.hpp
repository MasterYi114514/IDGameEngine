#pragma once

#include "Json.hpp"
#include "ArenaManager.hpp"

namespace ID
{
    namespace JSON
    {
        // 解析 JSON 字符串，返回 Json 对象
        Json parse(const std::string& json_str, ArenaID arena_id);
        Json parse(const char* json_str, ArenaID arena_id);
    } // namespace JSON
} // namespace ID