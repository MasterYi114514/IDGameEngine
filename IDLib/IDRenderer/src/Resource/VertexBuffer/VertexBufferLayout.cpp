#include "Resource/VertexBuffer/VertexBufferLayout.hpp"
#include "Log/Log.hpp"

namespace ID
{
    void VertexBufferLayout::calculate_stride()
    {
        static constexpr int float_size = sizeof(float);
        static constexpr int int_size   = sizeof(int);
        static constexpr int ubyte_size = sizeof(unsigned char); 

        m_stride = 0;
        for(auto& attr : m_attributes)
        {
            attr.offset = m_stride;
            switch(attr.type)
            {
                case AttributeType::Float:   m_stride +=     float_size;  break;
                case AttributeType::Float2:  m_stride += 2 * float_size;  break;
                case AttributeType::Float3:  m_stride += 3 * float_size;  break;
                case AttributeType::Float4:  m_stride += 4 * float_size;  break;
                case AttributeType::Int:     m_stride +=     int_size;    break;
                case AttributeType::Int2:    m_stride += 2 * int_size;    break;
                case AttributeType::Int3:    m_stride += 3 * int_size;    break;
                case AttributeType::Int4:    m_stride += 4 * int_size;    break;
                case AttributeType::UByte4:  m_stride += 4 * ubyte_size;  break;
            }
        }
    }

    void VertexBufferLayout::emplace_back(VertexBufferAttribute&& attr)
    {
        m_attributes.emplace_back(std::move(attr));
    }
} // namesapce ID