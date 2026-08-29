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

    /**
     *  因为每个 pipeline 一定有自己的 Shader
     *  但是却无法从 Shader 获取对于的 pipeline
     *  因此 Cache 中缓存当前 pipeline 绑定的 Shader
     */
    struct OpenGLCache
    {
        ID::ShaderID    current_shader = ID::ShaderID::invalid_id();
        GLuint          current_program = 0;

        ID::PipelineID  current_pipeline = ID::PipelineID::invalid_id();
        ID::ShaderID    current_pipeline_shader = ID::ShaderID::invalid_id();

        bool bind_pipeline(const ID::PipelineID pipeline)
        {
            if(current_pipeline != pipeline)
            {
                current_pipeline = pipeline;
                current_pipeline_shader = IDR_ResPipeline(pipeline)->get_shader_id();
                bind_shader(current_pipeline_shader);
                return false;
            }
            else if(current_pipeline_shader != current_shader)
            {
                bind_shader(current_pipeline_shader);
            }

            return true;
        }

        void bind_shader(const ID::ShaderID shader)
        {
            GLuint program = IDR_ResShader(shader)->get_program_id();
            if(current_shader != shader || current_program != program)
            {
                glUseProgram(program);
                current_shader = shader;
                current_program = program;
            }
        }
    } g_GLCache;
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
        // 清深度前必须确保 depth mask 可写：
        // 上一帧末尾的 Pass（如 Skybox/PostProcess）可能遗留 glDepthMask(GL_FALSE)，
        // 否则 glClear(GL_DEPTH_BUFFER_BIT) 不会真正清除深度缓冲。
        if (clear_depth)
        {
            glDepthMask(GL_TRUE);
        }

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

    void bind_default_framebuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // bind_framebuffer_color
    void bind_framebuffer_color(const FrameBufferID framebuffer, 
        uint32_t attachment, uint32_t slot)
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, IDR_ResFB(framebuffer)->get_color_attachment(attachment));
    }

    // bind_framebuffer_depth
    void bind_framebuffer_depth(const FrameBufferID framebuffer, uint32_t slot)
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, IDR_ResFB(framebuffer)->get_depth_attachment());
    }

    // blit_framebuffer：src → dst（颜色附件，尺寸一致）
    void blit_framebuffer(const FrameBufferID src, const FrameBufferID dst,
        uint32_t width, uint32_t height)
    {
        if(!src.is_valid() || !dst.is_valid()) return;
        if(width == 0 || height == 0) return;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, IDR_ResFB(src)->get_FBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, IDR_ResFB(dst)->get_FBO());
        glBlitFramebuffer(0, 0, static_cast<GLint>(width), static_cast<GLint>(height),
            0, 0, static_cast<GLint>(width), static_cast<GLint>(height),
            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // blit_framebuffer_to_default：src → 默认 framebuffer（窗口显示）
    void blit_framebuffer_to_default(const FrameBufferID src,
        uint32_t width, uint32_t height)
    {
        if(!src.is_valid()) return;
        if(width == 0 || height == 0) return;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, IDR_ResFB(src)->get_FBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, static_cast<GLint>(width), static_cast<GLint>(height),
            0, 0, static_cast<GLint>(width), static_cast<GLint>(height),
            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // blit_framebuffer_depth：src → dst（深度附件，尺寸一致）
    void blit_framebuffer_depth(const FrameBufferID src, const FrameBufferID dst,
        uint32_t width, uint32_t height)
    {
        if(!src.is_valid() || !dst.is_valid()) return;
        if(width == 0 || height == 0) return;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, IDR_ResFB(src)->get_FBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, IDR_ResFB(dst)->get_FBO());
        glBlitFramebuffer(0, 0, static_cast<GLint>(width), static_cast<GLint>(height),
            0, 0, static_cast<GLint>(width), static_cast<GLint>(height),
            GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // get_framebuffer_color_texture（带附件索引）：返回指定颜色附件的 GL 纹理句柄
    uint32_t get_framebuffer_color_texture(const FrameBufferID framebuffer, uint32_t attachment)
    {
        if(!framebuffer.is_valid()) return 0;
        return static_cast<uint32_t>(IDR_ResFB(framebuffer)->get_color_attachment(attachment));
    }

    // get_framebuffer_color_texture（旧单参版本）：委托带索引版本
    uint32_t get_framebuffer_color_texture(const FrameBufferID framebuffer)
    {
        return get_framebuffer_color_texture(framebuffer, 0);
    }

    void bind_pipeline(const PipelineID pipeline)
    {
        if(!g_GLCache.bind_pipeline(pipeline))
        {
            apply_pipeline_state(pipeline);
        }  
    }

    void bind_shader(const ID::ShaderID shader)
    {
        g_GLCache.bind_shader(shader);
    }

    // get_pipeline_layout：返回管线的顶点布局（供"同 layout 换 shader"的管线派生）
    const VertexBufferLayout& get_pipeline_layout(const PipelineID pipeline)
    {
        return IDR_ResPipeline(pipeline)->get_vertex_buffer_layout();
    }

    // get_pipeline_state：返回管线状态（同上）
    const PipelineState& get_pipeline_state(const PipelineID pipeline)
    {
        return IDR_ResPipeline(pipeline)->get_pipeline_state();
    }

    // draw_indexed
    void draw_indexed(const PipelineID pipeline, const VertexBufferID vb, const IndexBufferID ib)
    {
        bind_pipeline(pipeline);

        VertexBuffer* VBPtr = IDR_ResVB(vb);
        IndexBuffer* IBPtr = IDR_ResIB(ib);

        GLuint vao = IDR_ResPipeline(pipeline)->get_vao(VBPtr->get_vbo());
        glBindVertexArray(vao);

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
        bind_pipeline(pipeline);

        VertexBuffer* VBPtr = IDR_ResVB(vb);

        GLuint vao = IDR_ResPipeline(pipeline)->get_vao(VBPtr->get_vbo());
        glBindVertexArray(vao);

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