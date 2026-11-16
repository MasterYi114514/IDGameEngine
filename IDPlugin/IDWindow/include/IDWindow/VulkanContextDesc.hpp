#pragma once

/**
 *  Vulkan 上下文描述占位：本插件**不创建任何 Vulkan 对象**，也不含 Vulkan SDK 头。
 *
 *  当前没有字段（空 POD 占位）；后续只允许尾部追加字段，已冻结的形态是"可以按值传递的空描述符"。
 */

namespace ID::Window
{
    struct VulkanContextDesc
    {
    };
} // namespace ID::Window
