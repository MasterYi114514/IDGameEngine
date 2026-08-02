#pragma once

#include "IDWindowCore.hpp"
#include <string>

namespace ID
{
    struct IDWINDOW_API WindowProps
    {
        std::string title;          // 窗口标题

        unsigned int width;         // 窗口宽度
        unsigned int height;        // 窗口高度

        WindowProps(const std::string& title, unsigned int width, unsigned int height)
            : title(title), width(width), height(height) {}
    };
} // namespace ID
