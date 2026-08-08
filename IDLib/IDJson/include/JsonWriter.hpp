#pragma once

#include "Json.hpp"
#include <string>
#include <ostream>

namespace ID::JSON
{
    // 缩进格式
    enum class IndentStyle
    {
        Compact,            // 无空格无换行
        TwoSpace,           // 2 空格缩进
        FourSpace,          // 4 空格缩进（默认）
        Tab                 // \t 缩进
    };

    std::string to_string(const Json& value, IndentStyle indent = IndentStyle::FourSpace);

    void write_to_file(const std::string& path, const Json& json,
        IndentStyle indent = IndentStyle::FourSpace);

    void write(std::ostream& os, const Json& value, IndentStyle indent = IndentStyle::FourSpace);
} // namespace ID::JSON