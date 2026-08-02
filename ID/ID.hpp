#pragma once

// =====================================================================
//  ID.hpp  — 引擎聚合头文件
//      游戏项目（Sandbox）仅需 #include "ID.hpp"
// =====================================================================

// Core
#include "Core/IDCore.hpp"

// Events
#include "Events/Event.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Events/WindowEvent.hpp"
#include "Events/ApplicationEvent.hpp"

// Input
#include "Input/Input.hpp"
#include "Input/KeyCode.hpp"

// Window
#include "Window/WindowPool.hpp"

// Layer
#include "Layer/Layer.hpp"
#include "Layer/LayerStack.hpp"

// Application
#include "Application/Application.hpp"
#include "Application/Timestep.hpp"

// Camera
#include "Camera/Camera.hpp"
#include "Camera/CameraController.hpp"

// Renderer
#include "Renderer/IDRCore.hpp"

// Renderer/Geometry
#include "Renderer/Geometry/Geometry.hpp"

// Renderer/Light
#include "Renderer/Light/Light.hpp"
#include "Renderer/Light/LightUniforms.hpp"

// EntryPoint 入口（包含 main()，必须最后）
#include "Application/EntryPoint.hpp"
