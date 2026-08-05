#pragma once

// =====================================================================
//  ID.hpp  — 引擎聚合头文件
//      游戏项目（Sandbox）仅需 #include "ID.hpp"
// =====================================================================

// Core
#include "Core/IDCore.hpp"

// Events
#include "Events/Event.hpp"
#include "Events/EventDispatcher.hpp"
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

#include "Layer/CameraLayer.hpp"
#include "Layer/SceneLayer.hpp"
#include "Layer/RenderLayer.hpp"

// Application
#include "Application/Application.hpp"
#include "Application/Timestep.hpp"

// Camera
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Camera/CameraController.hpp"

// Renderer
#include "Renderer/IDRCore.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/RenderGraph.hpp"

// Renderer/Light
#include "Renderer/Light/Light.hpp"
#include "Renderer/Light/LightUniforms.hpp"

// Renderer/Material
#include "Renderer/Material/Material.hpp"
#include "Renderer/Material/MaterialParam.hpp"
#include "Renderer/Material/MaterialInstance.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"

// Renderer/Mesh
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Mesh/MeshData.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Mesh/Model.hpp"

// Renderer/RenderPass
#include "Renderer/RenderPass/RenderPass.hpp"
#include "Renderer/RenderPass/RenderPassContext.hpp"
#include "Renderer/RenderPass/ForwardPass.hpp"

// Scene
#include "Scene/Scene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

// Scene/Component
#include "Scene/Component/Component.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/Component/LightComponent.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"

// EntryPoint 入口（包含 main()，必须最后）
#include "Application/EntryPoint.hpp"
