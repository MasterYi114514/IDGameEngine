#pragma once

namespace ID
{
    enum class BufferUsageHint : uint8_t
    {
        None = 0,
        StaticDraw,     // 静态绘制，数据不会频繁修改
        DynamicDraw,    // 动态绘制，数据会频繁修改
        StreamDraw      // 流式绘制，数据每次绘制都会修改
    };

    struct UniformBufferCreateInfo
    {
        UniformBufferCreateInfo() = delete;
        UniformBufferCreateInfo(size_t size, uint32_t binding_point, BufferUsageHint usage_hint = BufferUsageHint::DynamicDraw)
            : size(size), binding_point(binding_point), usage_hint(usage_hint) {}
        

        size_t              size;                   // 缓冲区大小（字节数）
        uint32_t            binding_point;          // 绑定点编号
        BufferUsageHint     usage_hint;             // 缓冲区使用提示
    };
} // namespace ID