#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace ID
{
    // Json 类前向声明
    class Json;
}

namespace ID::JSON
{
    enum class Type : uint8_t
    {
        Null = 0,
        Bool,
        Int,
        Float,
        String,         
        ShortString,        // 对于小于等于 7 字节的字符串，会直接存储在void* 内，以 '\0' 结尾
        Array,
        Object
    };
}
