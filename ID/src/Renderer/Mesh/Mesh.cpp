#include "Renderer/Mesh/Mesh.hpp"

namespace ID
{
    Mesh::Mesh(MeshData& mesh_data) : m_layout(std::move(mesh_data.layout))
    {
        if(!mesh_data.is_valid()) return;

        VertexBufferCreateInfo vb_info(mesh_data.vertices_data.data(), mesh_data.get_vertex_count(),
            m_layout);
        m_vb = VBManager::create(vb_info);
        m_vertex_count = mesh_data.get_vertex_count();

        IndexBufferCreateInfo ib_info(mesh_data.indices.data(), 
            static_cast<uint32_t>(mesh_data.indices.size()));
        m_ib = IBManager::create(ib_info);

        m_index_count = static_cast<uint32_t>(mesh_data.indices.size());
    }

    void Mesh::destroy()
    {
        if(m_vb.is_valid())
        {
            VBManager::destroy(m_vb);
            m_vb = VertexBufferID::invalid_id();
        }

        if(m_ib.is_valid())
        {
            IBManager::destroy(m_ib);
            m_ib = IndexBufferID::invalid_id();
        }

        m_vertex_count = 0;
        m_index_count  = 0;
        m_layout.clear();
    }

    Mesh::Mesh(Mesh&& other) noexcept
    {
        std::swap(m_vb, other.m_vb);
        std::swap(m_ib, other.m_ib);
        std::swap(m_vertex_count, other.m_vertex_count);
        std::swap(m_index_count, other.m_index_count);
        std::swap(m_layout, other.m_layout);

        other.destroy();
    }

    Mesh& Mesh::operator=(Mesh&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_vb, other.m_vb);
            std::swap(m_ib, other.m_ib);
            std::swap(m_vertex_count, other.m_vertex_count);
            std::swap(m_index_count, other.m_index_count);
            std::swap(m_layout, other.m_layout);

            other.destroy();
        }
        return *this;
    }
} // namespace ID