#pragma once

/*
*   IDWindow  — 窗口管理库
*
*   提供窗口创建、事件系统、输入轮询等功能。
*   封装平台差异（GLFW / Win32 / SDL 等），通过编译宏切换后端。
*
*   用法：
*      #include <IDWindow.hpp>
*      链接 IDWindow.lib
*/

// ---- 核心 ----
#include "IDWindowCore.hpp"

// ---- 窗口 ----
#include "Window/WindowID.hpp"
#include "Window/WindowProps.hpp"
#include "Window/WindowPool.hpp"

// ---- 事件系统 ----
#include "Events/Event.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Events/WindowEvent.hpp"

// ---- 输入 ----
#include "Input/Input.hpp"
#include "Input/KeyCode.hpp"
