#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    /**
     *  FullscreenQuad：全屏三角形工具（Phase 4 新增）
     *
     *  后处理 Pass 绘制整屏特效用。使用 3 顶点大三角形（覆盖视口）而非四边形：
     *    - 避免四边形对角线的采样不均（插值权重不一致）
     *    - 无需索引缓冲，draw_arrays 一次绘制
     *    - UV 由 shader 从顶点位置推导（aPos * 0.5 + 0.5）
     *
     *  顶点数据为静态常量，VB 懒创建后进程内复用（static 局部，线程安全初始化）。
     */
    class ID_API FullscreenQuad
    {
    public:
        FullscreenQuad() = delete;
        ~FullscreenQuad() = delete;

        // 全屏三角形顶点缓冲（3 顶点，布局 aPos: Float2），懒创建
        // 注意：VertexBufferCreateInfo 需要 layout 的非 const 引用，故返回可变引用
        static VertexBufferID& vertex_buffer();

        // 全屏三角形布局：aPos(Float2)，stride = 8 字节
        static VertexBufferLayout& layout();
    };
} // namespace ID
