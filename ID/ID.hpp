#pragma once

// =====================================================================
//  ID.hpp  — 引擎聚合头文件
//      游戏项目（Sandbox）仅需 #include "ID.hpp"
// =====================================================================

// Core
#include "Core/IDCore.hpp"
#include "Core/BasicID.hpp"
#include "Core/IDArray.hpp"
#include "Core/SerializableBase.hpp"

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

// Application
#include "Application/Application.hpp"
#include "Application/Timestep.hpp"

// Camera
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Camera/CameraController.hpp"
#include "Renderer/Camera/ProjectionParams.hpp"

// Renderer
#include "Renderer/IDRCore.hpp"
#include "Renderer/Pose.hpp"

// Renderer/Render
#include "Renderer/Render/Renderer.hpp"
#include "Renderer/Render/RenderGraph.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Render/FullscreenQuad.hpp"

// Renderer/Render/RenderPass
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderPass/ForwardPass.hpp"
#include "Renderer/Render/RenderPass/ShadowPass.hpp"
#include "Renderer/Render/RenderPass/SkyboxPass.hpp"
#include "Renderer/Render/RenderPass/TransparentPass.hpp"
#include "Renderer/Render/RenderPass/PostProcessPass.hpp"

// Renderer/Light
#include "Renderer/Light/Light.hpp"
#include "Renderer/Light/LightUniforms.hpp"

// Renderer/Shadow
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Shadow/ShadowCamera.hpp"
#include "Renderer/Shadow/ShadowMap.hpp"
#include "Renderer/Shadow/ShadowManager.hpp"

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

// Renderer/Resource
#include "Renderer/Resource/ShaderManager.hpp"
#include "Renderer/Resource/TextureManager.hpp"

// Scene
#include "Scene/Scene.hpp"
#include "Scene/SceneID.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/AssetManager.hpp"

// Scene/Audio
#include "Scene/Audio/AudioID.hpp"
#include "Scene/Audio/AudioManager.hpp"

// Scene/Component
#include "Scene/Component/Component.hpp"
#include "Scene/Component/ComponentFactory.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/Component/LightComponent.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"
#include "Scene/Component/RigidBodyComponent.hpp"
#include "Scene/Component/AudioSourceComponent.hpp"
#include "Scene/Component/AudioListenerComponent.hpp"

// Scene/System
#include "Scene/System/System.hpp"
#include "Scene/System/PhysicsSystem.hpp"

// DevGUI/ImGui
#include "DevGUI/ImGui/ImGuiLayer.hpp"
#include "DevGUI/ImGui/Panels/ImGuiPanel.hpp"

// EntryPoint 入口（包含 main()，必须最后）
#include "Application/EntryPoint.hpp"
