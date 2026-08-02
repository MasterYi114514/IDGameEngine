#pragma once

namespace ID
{
    /**
     *  混合因子：用于控制颜色混合时的源颜色和目标颜色的权重
     *  - `Zero`: 源颜色的权重为0，表示不使用源颜色
     *  - `One`: 源颜色的权重为1，表示完全使用源颜色
     *  - `SrcAlpha`: 源颜色的权重为源颜色的alpha值，表示根据源颜色的透明度来混合
     *  - `OneMinusSrcAlpha`: 源颜色的权重为1减去源颜色的alpha值，表示根据源颜色的透明度来混合
     *  - `DstAlpha`: 源颜色的权重为目标颜色的alpha值，表示根据目标颜色的透明度来混合
     *  - `OneMinusDstAlpha`: 源颜色的权重为1减去目标颜色的alpha值，表示根据目标颜色的透明度来混合
     */
    enum class BlendFactor : uint8_t
    {
        Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha
    };

    /**
     *  深度比较：决定深度测试的时候如何比较新旧深度值
     *  - `Never`: 永远不通过深度测试
     *  - `Less`: 新深度值小于旧深度值时通过深度测试
     *  - `Equal`: 新深度值等于旧深度值时通过深度测试
     *  - `LessEqual`: 新深度值小于或等于旧深度值时通过深度测试
     *  - `Greater`: 新深度值大于旧深度值时通过深度测试
     *  - `NotEqual`: 新深度值不等于旧深度值时通过深度测试
     *  - `GreaterEqual`: 新深度值大于或等于旧深度值时通过深度测试
     *  - `Always`: 永远通过深度测试（即关闭深度测试）
     */
    enum class DepthFunc : uint8_t
    {
        Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always
    };

    /**
     *  剔除模式：决定在渲染时是否剔除某些面（正面指逆时针的面，背面指顺时针的面）
     *  - `None`: 不进行剔除，渲染所有面
     *  - `Front`: 剔除正面，渲染背面
     *  - `Back`: 剔除背面，渲染正面
     */
    enum class CullMode : uint8_t
    {
        None, Front, Back
    };

    /**
     *  图元类型：决定渲染时使用的图元类型
     *  - `Triangles`: 三角形图元，每三个顶点组成一个三角形
     *  - `TriangleStrip`: 三角形条图元，新三角形复用前一个三角形的两个顶点
     *  - `Lines`: 线段图元，每两个顶点组成一条线段
     *  - `Points`: 点图元，每个顶点渲染为一个点
     */
    enum class PrimitiveType : uint8_t
    {
        Triangles, TriangleStrip, Lines, Points
    };

    /**
     *  管线状态：封装渲染管线的光栅化、混合状态
     *  - `depth_test`: 是否启用深度测试（近处遮挡远处）
     *  - `depth_write`: 是否启用深度写入（更新深度缓冲区，一般只有半透明物体需要关掉）
     *  - `depth_func`: 深度比较函数，决定深度测试时如何比较新旧深度值
     *  - `cull_mode`: 剔除模式，决定是否剔除某些面（背面或正面）
     *  - `blend`: 是否启用颜色混合（半透明物体需要开启）
     *  - `blend_src`: 源颜色混合因子，决定源颜色在混合中的权重
     *  - `blend_dst`: 目标颜色混合因子，决定目标颜色在混合中的权重
     *  - `primitive`: 图元类型，决定渲染时使用的图元类型（如三角形、线段、点等）
     */
    struct PipelineState
    {
        bool          depth_test   = true;
        bool          depth_write  = true;
        DepthFunc     depth_func   = DepthFunc::Less;
        CullMode      cull_mode    = CullMode::Back;
        bool          blend        = false;
        BlendFactor   blend_src    = BlendFactor::SrcAlpha;
        BlendFactor   blend_dst    = BlendFactor::OneMinusSrcAlpha;
        PrimitiveType primitive    = PrimitiveType::Triangles;
    };
}