#include "Resource/Pipeline/Pipeline.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace ID
{
    Pipeline::Pipeline(const PipelineCreateInfo& createInfo)
        : m_shaderID(createInfo.shaderID), m_layout(createInfo.layout), m_pipelineState(createInfo.pipelineState) { }

    void Pipeline::destroy()
    {
        // 销毁所有缓存的 VAO
        for (const auto& pair : vaoMap)
        {
            glDeleteVertexArrays(1, &pair.second);
        }
        vaoMap.clear();

        m_shaderID = ShaderID::invalid_id();
    }

    Pipeline::Pipeline(Pipeline&& other) noexcept
        : m_shaderID(other.m_shaderID), m_layout(other.m_layout), m_pipelineState(other.m_pipelineState), vaoMap(std::move(other.vaoMap))
    {
        other.m_shaderID = ShaderID::invalid_id();
        other.vaoMap.clear();
    }

    Pipeline& Pipeline::operator=(Pipeline&& other) noexcept
    {
        if (this != &other)
        {
            destroy(); // 销毁当前对象的资源

            m_shaderID = other.m_shaderID;
            m_layout = other.m_layout;
            m_pipelineState = other.m_pipelineState;
            vaoMap = std::move(other.vaoMap);

            other.m_shaderID = ShaderID::invalid_id();
            other.vaoMap.clear();
        }
        return *this;
    }

    GLuint Pipeline::get_vao(GLuint vbo) const
    {
        // 检查是否已经缓存了对应的 VAO
        auto it = vaoMap.find(vbo);
        if (it != vaoMap.end())
        {
            return it->second;          // 返回缓存的 VAO
        }

        // 创建新的 VAO
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        // 绑定 VBO
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        uint32_t attr_index = 0;
        for(const auto& attr : m_layout)
        {
            glEnableVertexAttribArray(attr_index);

            uint32_t        count = 0;
            GLuint          gl_type = GL_FLOAT;
            bool            normalized = false;

            switch (attr.type)
            {
                case AttributeType::Float:      count = 1; gl_type = GL_FLOAT;                      break;
                case AttributeType::Float2:     count = 2; gl_type = GL_FLOAT;                      break;
                case AttributeType::Float3:     count = 3; gl_type = GL_FLOAT;                      break;
                case AttributeType::Float4:     count = 4; gl_type = GL_FLOAT;                      break;
                case AttributeType::Int:        count = 1; gl_type = GL_INT;                        break;
                case AttributeType::Int2:       count = 2; gl_type = GL_INT;                        break;
                case AttributeType::Int3:       count = 3; gl_type = GL_INT;                        break;
                case AttributeType::Int4:       count = 4; gl_type = GL_INT;                        break;
                case AttributeType::UByte4:     count = 4; gl_type = GL_UNSIGNED_BYTE; 
                                                normalized = true;                                  break;
            }

            if (gl_type == GL_FLOAT)
            {
                glVertexAttribPointer(
                    attr_index, static_cast<int>(count), gl_type,
                    normalized ? GL_TRUE : GL_FALSE,
                    static_cast<int>(m_layout.get_stride()),
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset)));
            }
            else
            {
                glVertexAttribIPointer(
                    attr_index, static_cast<int>(count), gl_type,
                    static_cast<int>(m_layout.get_stride()),
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset)));
            }

            ++attr_index;
        }

        // 解绑 VAO 和 VBO
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // 缓存 VAO
        vaoMap[vbo] = vao;

        return vao;
    }
} // namespace ID

#endif