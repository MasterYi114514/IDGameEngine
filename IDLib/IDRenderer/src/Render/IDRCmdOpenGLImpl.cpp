#include "Render/RenderCommand.hpp"
#include "Resource/ResourceGetter.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace
{
    void apply_depth_state(const ID::PipelineState& state)
    {
        if(state.depth_test)
        {
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }

        glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);

        switch(state.depth_func)
        {
            case ID::DepthFunc::Never:          glDepthFunc(GL_NEVER);      break;
            case ID::DepthFunc::Less:           glDepthFunc(GL_LESS);       break;
            case ID::DepthFunc::Equal:          glDepthFunc(GL_EQUAL);      break;
            case ID::DepthFunc::LessEqual:      glDepthFunc(GL_LEQUAL);     break;
            case ID::DepthFunc::Greater:        glDepthFunc(GL_GREATER);    break;
            case ID::DepthFunc::NotEqual:       glDepthFunc(GL_NOTEQUAL);   break;
            case ID::DepthFunc::GreaterEqual:   glDepthFunc(GL_GEQUAL);     break;
            case ID::DepthFunc::Always:         glDepthFunc(GL_ALWAYS);     break;
        }
    }

    void apply_blend_state(const ID::PipelineState& state)
    {
        if(state.blend)
        {
            glEnable(GL_BLEND);
            
            static auto to_gl = [](ID::BlendFactor factor) -> GLenum
            {
                switch(factor)
                {
                    case ID::BlendFactor::Zero:                 return GL_ZERO;
                    case ID::BlendFactor::One:                  return GL_ONE;
                    case ID::BlendFactor::SrcAlpha:             return GL_SRC_ALPHA;
                    case ID::BlendFactor::OneMinusSrcAlpha:     return GL_ONE_MINUS_SRC_ALPHA;
                    case ID::BlendFactor::DstAlpha:             return GL_DST_ALPHA;
                    case ID::BlendFactor::OneMinusDstAlpha:     return GL_ONE_MINUS_DST_ALPHA;
                    default:                                    return GL_ONE;
                }
            };
            glBlendFunc(to_gl(state.blend_src), to_gl(state.blend_dst));
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    void apply_cull_state(const ID::PipelineState& state)
    {
        switch(state.cull_mode)
        {
            case ID::CullMode::None:  glDisable(GL_CULL_FACE);                          break;
            case ID::CullMode::Front: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);     break;
            case ID::CullMode::Back:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK);      break;
        }
    }

    void apply_pipeline_state(const ID::PipelineID pipeline)
    {
        auto state = IDR_ResPipeline(pipeline)->get_pipeline_state();
        apply_depth_state(state);
        apply_blend_state(state);
        apply_cull_state(state);
    }
} // 匿名命名空间

namespace ID::RenderCommand
{
    // set_clear_color
    void set_clear_color(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
    }

    void set_clear_color(const Vec3& color, float a)
    {
        set_clear_color(color[0], color[1], color[2], a);
    }

    void set_clear_color(const Vec4& color)
    {
        set_clear_color(color[0], color[1], color[2], color[3]);
    }

    // clear
    void clear(bool clear_color, bool clear_depth)
    {
        GLbitfield mask = 0;
        if (clear_color) mask |= GL_COLOR_BUFFER_BIT;
        if (clear_depth) mask |= GL_DEPTH_BUFFER_BIT;
        glClear(mask);
    }

    // set_viewport
    void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        glViewport(x, y, width, height);
    }

    // bind_framebuffer
    void bind_framebuffer(const FrameBufferID framebuffer)
    {
        GLuint fbo = IDR_ResFB(framebuffer)->get_FBO();
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }

    // draw_indexed
    void draw_indexed(const PipelineID pipeline, const VertexBufferID vb, const IndexBufferID ib)
    {
        Pipeline* PipelinePtr = IDR_ResPipeline(pipeline);
        VertexBuffer* VBPtr = IDR_ResVB(vb);
        IndexBuffer* IBPtr = IDR_ResIB(ib);

        GLuint program = IDR_ResShader(PipelinePtr->get_shader_id())->get_program_id();
        glUseProgram(program);

        GLuint vao = PipelinePtr->get_vao(VBPtr->get_vbo());
        glBindVertexArray(vao);

        apply_pipeline_state(pipeline);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBPtr->get_ibo());
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(IBPtr->get_index_count()), GL_UNSIGNED_INT, nullptr);
    }

    // draw_arrays
    void draw_arrays(const PipelineID pipeline, const VertexBufferID vb)
    {
        uint32_t vertex_count = IDR_ResVB(vb)->get_vertex_count();
        draw_arrays(pipeline, vb, 0, vertex_count);
    }

    void draw_arrays(const PipelineID pipeline, const VertexBufferID vb, 
        uint32_t first_vertex, uint32_t vertex_count)
    {
        Pipeline* PipelinePtr = IDR_ResPipeline(pipeline);
        VertexBuffer* VBPtr = IDR_ResVB(vb);

        GLuint program = IDR_ResShader(PipelinePtr->get_shader_id())->get_program_id();
        glUseProgram(program);

        GLuint vao = PipelinePtr->get_vao(VBPtr->get_vbo());
        glBindVertexArray(vao);

        apply_pipeline_state(pipeline);

        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first_vertex), static_cast<GLsizei>(vertex_count));
    }

    // bind_texture
    void bind_texture(const TextureID texture, uint32_t slot)
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, IDR_ResTexture(texture)->get_texture_id());
    }

    // unbind_texture
    void unbind_texture(uint32_t slot)
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

} // namespace ID::RenderCommand

#endif // IDRENDERER_USE_OPENGL