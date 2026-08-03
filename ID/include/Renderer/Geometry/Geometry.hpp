#pragma once

#include "Renderer/IDRCore.hpp"

namespace ID
{
    enum class GeometryType
    {
        None = 0,           // 无效类型
        Cuboid,             // 长方体，接收 长、宽、高 参数
        Cube,               // 正方体，接收 边长 参数
        Sphere,             // 球体，接收 半径、经度分段数、纬度分段数 参数
    };

    // 用于描述几何体的位置、旋转、缩放等变换信息
    struct GeometryPose
    {
        Pos3 position = Pos3(0.0f, 0.0f, 0.0f);   // 位置
    };

    class ID_API Geometry
    {
    public:
        Geometry() = delete;
        Geometry(VertexBufferID vb, IndexBufferID ib) : m_VB(vb), m_IB(ib) {}
        ~Geometry() = default;

        Geometry(const Geometry&) = delete;
        Geometry& operator=(const Geometry&) = delete;

        Geometry(Geometry&&) = default;
        Geometry& operator=(Geometry&&) = default;

    public:
    
        VertexBufferID get_vb() const { return m_VB; }
        IndexBufferID  get_ib() const { return m_IB; }
        uint32_t get_vertex_count() const { return VBManager::get_vertex_count(m_VB); }
        static VertexBufferLayout& get_layout() { return s_layout; }
        
        static void set_layout(const VertexBufferLayout& layout);

    public:
        // 工厂函数
        static Geometry* create_cuboid(float width, float height, float depth);
        static Geometry* create_cube(float side_length);

        /**
         *  创建球体的工厂函数
         *  @param radius 球体半径
         *  @param longitude_segments 经度分段数（沿着球体的经线方向的分段数）
         *  @param latitude_segments 纬度分段数（沿着球体的纬线方向的分段数）
         */
        static Geometry* create_sphere(float radius, uint32_t longitude_segments, uint32_t latitude_segments);

    private:
        VertexBufferID                  m_VB;
        IndexBufferID                   m_IB;


        static VertexBufferLayout       s_layout;
    };
} // namespace