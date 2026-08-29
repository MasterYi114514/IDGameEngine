#pragma once

// Core
#include "Core/IDRendererCore.hpp"
#include "Core/IDRpch.hpp"
#include "Core/ResourceType.hpp"

// Resource
#include "Resource/ResourceID.hpp"
#include "Resource/ResourceManager.hpp"
using VBManager         = ID::ResourceManager<ID::VertexBufferUINT,     ID::ResourceType::VertexBuffer>;
using IBManager         = ID::ResourceManager<ID::IndexBufferUINT,      ID::ResourceType::IndexBuffer>;
using ShaderManager     = ID::ResourceManager<ID::ShaderUINT,           ID::ResourceType::Shader>;
using TextureManager    = ID::ResourceManager<ID::TextureUINT,          ID::ResourceType::Texture>;
using PipelineManager   = ID::ResourceManager<ID::PipelineUINT,         ID::ResourceType::Pipeline>;
using FBManager         = ID::ResourceManager<ID::FrameBufferUINT,      ID::ResourceType::FrameBuffer>;
using UBManager         = ID::ResourceManager<ID::UniformBufferUINT,    ID::ResourceType::UniformBuffer>;

// Resource/VertexBuffer
#include "Resource/VertexBuffer/VertexBufferAttribute.hpp"
#include "Resource/VertexBuffer/VertexBufferLayout.hpp"
#include "Resource/VertexBuffer/VertexBufferCreateInfo.hpp"

// Resource/IndexBuffer
#include "Resource/IndexBuffer/IndexBufferCreateInfo.hpp"

// Resource/Shader
#include "Resource/Shader/ShaderSourceLoader.hpp"
#include "Resource/Shader/ShaderParamConcept.hpp"
#include "Resource/Shader/ShaderCreateInfo.hpp"
#include "Resource/Shader/ShaderUniformDesc.hpp"

// Resource/Texture
#include "Resource/Texture/TextureCreateInfo.hpp"

// Resource/Pipeline
#include "Resource/Pipeline/PipelineState.hpp"
#include "Resource/Pipeline/PipelineCreateInfo.hpp"

// Resource/Framebuffer
#include "Resource/Framebuffer/FramebufferCreateInfo.hpp"

// Resource/UniformBuffer
#include "Resource/UniformBuffer/UniformBufferCreateInfo.hpp"

// RenderCommand
#include "Render/RenderCommand.hpp"
#include "Render/SetParamImpl.hpp"
namespace IDRCmd = ID::RenderCommand;