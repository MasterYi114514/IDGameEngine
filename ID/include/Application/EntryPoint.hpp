#pragma once

#include <Application/Application.hpp>
#include <IDMath.hpp>

int main()
{
    ID::Application* app = ID::create_application();
    app->run();

    return 0;
}
