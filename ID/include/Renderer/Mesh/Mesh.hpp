#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/MeshData.hpp"

namespace ID
{
    class MeshFactory;

    class ID_API Mesh
    {
    public:
        Mesh() = delete;
        ~Mesh() { destroy(); }

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&&) noexcept;
        Mesh& operator=(Mesh&&) noexcept;

        void destroy();

    public:
        VertexBufferID              get_vb()            const { return m_vb; }
        IndexBufferID               get_ib()            const { return m_ib; }
        uint32_t                    get_vertex_count()  const { return m_vertex_count; }
        uint32_t                    get_index_count()   const { return m_index_count; }
        const VertexBufferLayout&   get_layout()        const { return m_layout; }
        
    private:
        friend class MeshFactory;
        Mesh(MeshData& mesh_data);

    private:
        VertexBufferID      m_vb;
        IndexBufferID       m_ib;
        uint32_t            m_vertex_count = 0;
        uint32_t            m_index_count  = 0;
        VertexBufferLayout  m_layout;
    };
} // namespace ID