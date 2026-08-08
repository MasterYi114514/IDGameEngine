#pragma once

#include <Application/Application.hpp>
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
    ID::Application* app = ID::create_application();
    app->run();

    return 0;
}
