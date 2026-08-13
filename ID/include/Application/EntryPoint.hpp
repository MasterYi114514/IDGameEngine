#pragma once

#include <Application/Application.hpp>
#include <Log/Log.hpp>
#include <IDMath.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Json.hpp"
#include "JsonParser.hpp"
#include "JsonWriter.hpp"
#include "ArenaManager.hpp"

int main()
{
    ID_INFO("[EntryPoint] ===== main() 开始 =====");
    ID::Application* app = ID::create_application();
    ID_INFO("[EntryPoint] create_application() 完成，进入主循环");
    app->run();

    return 0;
}
