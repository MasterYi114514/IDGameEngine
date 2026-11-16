// Public-header self-containment probe: this TU includes exactly one IDWindow public header
// and then the dependency guard, which #errors if GLFW / platform / Vulkan / IDLog headers leak in.
#include "IDWindow/KeyCode/KeyCodes.hpp"

#include "window/window_header_probe_check.hpp"
