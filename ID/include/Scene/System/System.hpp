#pragma once

#include "IDpch.hpp"

namespace ID
{
    class Scene;

    class System
    {
    public:
        virtual ~System() = default;

        virtual void on_attach(Scene* scene = nullptr) = 0;
        virtual void on_detach() = 0;

        virtual void on_update(Timestep ts) = 0;
        virtual void on_event(Event& event) = 0;
    };
} // namespace ID