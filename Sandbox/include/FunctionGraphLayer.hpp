#pragma once

#include "ID.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <vector>
#include <array>
#include <fstream>

// =====================================================================
//  FunctionGraphLayer
//    函数图像绘制 + ImGui 参数面板
//    - 支持多种函数类型（sin / cos / 多项式 / exp）
//    - 拖动滑块实时改变参数，曲线即时更新
//    - 鼠标滚轮缩放，右键拖动平移
// =====================================================================
class FunctionGraphLayer : public ID::Layer
{
public:
    FunctionGraphLayer()
        : Layer("FunctionGraphLayer")
    {
    }

    // ===================== 生命周期 =====================

    void on_attach() override
    {
        // ---- ImGui 初始化 ----
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // ---- 加载中文字体（Windows 系统字体，放在 backend init 之前）----
        const char* font_path = "C:\\Windows\\Fonts\\msyh.ttc";
        std::ifstream font_test(font_path, std::ios::binary);
        if(font_test.good())
        {
            font_test.close();
            io.Fonts->AddFontFromFileTTF(font_path, 18.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            std::cout << "[FunctionGraph] 中文字体加载成功: " << font_path << "\n";
        }
        else
        {
            font_path = "C:\\Windows\\Fonts\\simhei.ttf";
            std::ifstream font_test2(font_path, std::ios::binary);
            if(font_test2.good())
            {
                font_test2.close();
                io.Fonts->AddFontFromFileTTF(font_path, 18.0f, nullptr,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
                std::cout << "[FunctionGraph] 中文字体加载成功(备选): " << font_path << "\n";
            }
            else
            {
                std::cerr << "[FunctionGraph] 警告: 未找到中文字体，中文将显示为问号\n";
            }
        }
        // 注意：不要调用 io.Fonts->Build()！backend 会在首次 NewFrame() 时自动构建

        ImGui::StyleColorsDark();

        // backend 初始化（顺序：先 GLFW，再 OpenGL3）
        ImGui_ImplGlfw_InitForOpenGL(
            (GLFWwindow*)ID::WindowPool::get_native_handle(), true);
        ImGui_ImplOpenGL3_Init("#version 330");

        // ---- 编译 Shader ----
        compile_shader();

        // ---- 创建 VAO + VBO ----
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_grid_vbo);

        generate_grid_vertices();

        std::cout << "[FunctionGraphLayer] 初始化完成\n";
    }

    void on_detach() override
    {
        glDeleteBuffers(1, &m_grid_vbo);
        glDeleteBuffers(1, &m_vbo);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteProgram(m_shader_program);

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    // ===================== 每帧更新 =====================

    void on_update(ID::Timestep ts) override
    {
        uint32_t w = ID::WindowPool::get_width();
        uint32_t h = ID::WindowPool::get_height();
        if(w == 0 || h == 0) return;

        float aspect = (float)w / (float)h;
        float half_h = m_view_scale;
        float half_w = half_h * aspect;

        float left   = m_view_center_x - half_w;
        float right  = m_view_center_x + half_w;
        float bottom = m_view_center_y - half_h;
        float top    = m_view_center_y + half_h;

        // ---- 视口 + 清屏 ----
        glViewport(0, 0, (int)w, (int)h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ---- 正交投影矩阵 ----
        glm::mat4 proj = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);

        glUseProgram(m_shader_program);
        glUniformMatrix4fv(m_loc_mvp, 1, GL_FALSE, glm::value_ptr(proj));

        glBindVertexArray(m_vao);
        glEnableVertexAttribArray(0);

        // ---- ① 绘制网格 ----
        glUniform3f(m_loc_color, 0.12f, 0.12f, 0.15f);
        glBindBuffer(GL_ARRAY_BUFFER, m_grid_vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_LINES, 0, m_grid_vertex_count);

        // ---- ② 绘制坐标轴 ----
        float axis_verts[] = {
            left,  0.0f,  right, 0.0f,   // X 轴
            0.0f,  bottom, 0.0f, top,     // Y 轴
        };
        glUniform3f(m_loc_color, 0.45f, 0.45f, 0.50f);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(axis_verts), axis_verts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glLineWidth(1.5f);
        glDrawArrays(GL_LINES, 0, 4);
        glLineWidth(1.0f);

        // ---- ③ 采样函数 + 绘制曲线 ----
        std::vector<float> curve_verts;
        curve_verts.reserve(m_sample_count * 2);
        float x_step = (right - left) / (float)(m_sample_count - 1);
        for(int i = 0; i < m_sample_count; i++)
        {
            float x = left + x_step * (float)i;
            float y = evaluate(x);
            curve_verts.push_back(x);
            curve_verts.push_back(y);
        }

        glUniform3f(m_loc_color, 0.25f, 0.90f, 0.40f);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
            curve_verts.size() * sizeof(float),
            curve_verts.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glLineWidth(2.5f);
        glDrawArrays(GL_LINE_STRIP, 0, m_sample_count);
        glLineWidth(1.0f);

        glBindVertexArray(0);
        glUseProgram(0);

        // ---- ④ ImGui 渲染 ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        // 修复：ImGui 的 GLFW backend 中 glfwGetWindowSize 可能返回 0
        // 直接用 IDWindow 的尺寸覆盖
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);

        ImGui::NewFrame();

        draw_imgui_panel();

        ImGui::Render();

        // 诊断：检查 draw data
        static int diag_frame = 0;
        if(diag_frame < 3)
        {
            diag_frame++;
            ImDrawData* dd = ImGui::GetDrawData();
            std::cout << "[FunctionGraph] Frame " << diag_frame
                      << ": DrawData valid=" << (dd->Valid ? "yes" : "no")
                      << ", CmdLists=" << dd->CmdListsCount
                      << ", TotalVtx=" << dd->TotalVtxCount
                      << ", TotalIdx=" << dd->TotalIdxCount
                      << ", DisplaySize=" << ImGui::GetIO().DisplaySize.x
                      << "x" << ImGui::GetIO().DisplaySize.y
                      << std::endl;

            // 检查 GL 错误
            GLenum err = glGetError();
            if(err != GL_NO_ERROR)
                std::cerr << "[FunctionGraph] GL Error before RenderDrawData: 0x"
                          << std::hex << err << std::dec << std::endl;
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // ===================== 事件处理 =====================

    void on_event(ID::Event& event) override
    {
        // 如果 ImGui 正在捕获鼠标/键盘，不处理视口操作
        ImGuiIO& io = ImGui::GetIO();

        ID::EventDispatcher dispatcher(event);

        // 鼠标滚轮 → 缩放
        dispatcher.dispatch<ID::MouseScrolledEvent>(
            [this, &io](ID::MouseScrolledEvent& e)
            {
                if(io.WantCaptureMouse) return false;
                float factor = (e.get_y_offset() > 0) ? 0.9f : 1.1f;
                m_view_scale *= factor;
                m_view_scale = std::clamp(m_view_scale, 0.1f, 100.0f);
                return true;
            });

        // 右键拖动 → 平移
        dispatcher.dispatch<ID::MouseButtonPressedEvent>(
            [this, &io](ID::MouseButtonPressedEvent& e)
            {
                if(io.WantCaptureMouse) return false;
                if(e.get_button() == 1)  // GLFW_MOUSE_BUTTON_RIGHT
                {
                    m_is_panning = true;
                    m_last_mouse_x = 0.0f;
                    m_last_mouse_y = 0.0f;
                    return true;
                }
                return false;
            });

        dispatcher.dispatch<ID::MouseButtonReleasedEvent>(
            [this, &io](ID::MouseButtonReleasedEvent& e)
            {
                if(e.get_button() == 1)
                {
                    m_is_panning = false;
                    return true;
                }
                return false;
            });

        dispatcher.dispatch<ID::MouseMovedEvent>(
            [this, &io](ID::MouseMovedEvent& e)
            {
                if(!m_is_panning) return false;
                {
                    float cx = e.get_x();
                    float cy = e.get_y();

                    if(m_last_mouse_x != 0.0f || m_last_mouse_y != 0.0f)
                    {
                        float w = (float)ID::WindowPool::get_width();
                        float h = (float)ID::WindowPool::get_height();
                        float aspect = w / h;
                        float scale = (2.0f * m_view_scale * aspect) / w;

                        m_view_center_x -= (cx - m_last_mouse_x) * scale;
                        m_view_center_y += (cy - m_last_mouse_y) * scale;
                    }

                    m_last_mouse_x = cx;
                    m_last_mouse_y = cy;
                    return true;
                }
                return false;
            });
    }

private:
    // ===================== Shader =====================

    GLuint m_shader_program = 0;
    GLint  m_loc_mvp   = -1;
    GLint  m_loc_color = -1;

    void compile_shader()
    {
        const char* vertex_src = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(aPos, 0.0, 1.0);
}
)";
        const char* fragment_src = R"(
#version 330 core
uniform vec3 u_color;
out vec4 FragColor;
void main() {
    FragColor = vec4(u_color, 1.0);
}
)";

        auto compile = [](GLenum type, const char* src) -> GLuint
        {
            GLuint id = glCreateShader(type);
            glShaderSource(id, 1, &src, nullptr);
            glCompileShader(id);
            GLint ok;
            glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
            if(!ok)
            {
                char log[512];
                glGetShaderInfoLog(id, 512, nullptr, log);
                std::cerr << "[FunctionGraph] Shader compile error: " << log << "\n";
            }
            return id;
        };

        GLuint vs = compile(GL_VERTEX_SHADER, vertex_src);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_src);

        m_shader_program = glCreateProgram();
        glAttachShader(m_shader_program, vs);
        glAttachShader(m_shader_program, fs);
        glLinkProgram(m_shader_program);

        GLint ok;
        glGetProgramiv(m_shader_program, GL_LINK_STATUS, &ok);
        if(!ok)
        {
            char log[512];
            glGetProgramInfoLog(m_shader_program, 512, nullptr, log);
            std::cerr << "[FunctionGraph] Shader link error: " << log << "\n";
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        m_loc_mvp   = glGetUniformLocation(m_shader_program, "u_mvp");
        m_loc_color = glGetUniformLocation(m_shader_program, "u_color");
    }

    // ===================== VAO / VBO =====================

    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    // ===================== 网格 =====================

    GLuint m_grid_vbo = 0;
    int    m_grid_vertex_count = 0;

    void generate_grid_vertices()
    {
        // 生成 [-20, 20] 范围的网格线，步长 1（共 82 条线 = 164 顶点）
        std::vector<float> verts;
        float range = 20.0f;
        for(int i = -20; i <= 20; i++)
        {
            float fi = (float)i;
            // 竖线：x = i
            verts.insert(verts.end(), { fi, -range, fi, range });
            // 横线：y = i
            verts.insert(verts.end(), { -range, fi, range, fi });
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_grid_vbo);
        glBufferData(GL_ARRAY_BUFFER,
            verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        m_grid_vertex_count = (int)verts.size() / 2;
    }

    // ===================== 视图参数 =====================

    float m_view_center_x = 0.0f;
    float m_view_center_y = 0.0f;
    float m_view_scale    = 6.0f;
    int   m_sample_count  = 2000;

    // 右键拖动
    bool  m_is_panning    = false;
    float m_last_mouse_x  = 0.0f;
    float m_last_mouse_y  = 0.0f;

    // ===================== 函数参数 =====================

    int   m_func_type = 0;   // 0=sin, 1=cos, 2=多项式, 3=exp
    float m_param_a   = 1.0f;
    float m_param_b   = 1.0f;
    float m_param_c   = 0.0f;
    float m_param_d   = 0.0f;

    float evaluate(float x) const
    {
        switch(m_func_type)
        {
        case 0:  return m_param_a * std::sin(m_param_b * x + m_param_c) + m_param_d;
        case 1:  return m_param_a * std::cos(m_param_b * x + m_param_c) + m_param_d;
        case 2:  return m_param_a * x*x*x + m_param_b * x*x + m_param_c * x + m_param_d;
        case 3:  return m_param_a * std::exp(m_param_b * x) + m_param_c * x + m_param_d;
        default: return 0.0f;
        }
    }

    // ===================== ImGui 面板 =====================

    void draw_imgui_panel()
    {
        // 确保窗口首次出现时在可见位置
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_FirstUseEver);

        bool show = true;
        ImGui::Begin("函数参数", &show,
            ImGuiWindowFlags_AlwaysAutoResize);

        const char* func_names[] = {
            "a·sin(b·x + c) + d",
            "a·cos(b·x + c) + d",
            "a·x³ + b·x² + c·x + d",
            "a·e^(b·x) + c·x + d"
        };
        ImGui::Combo("函数类型", &m_func_type, func_names, 4);

        ImGui::Separator();

        ImGui::DragFloat("a", &m_param_a, 0.01f, -10.0f, 10.0f, "%.3f");
        ImGui::DragFloat("b", &m_param_b, 0.01f, -10.0f, 10.0f, "%.3f");
        ImGui::DragFloat("c", &m_param_c, 0.01f, -10.0f, 10.0f, "%.3f");
        ImGui::DragFloat("d", &m_param_d, 0.1f,  -10.0f, 10.0f, "%.3f");

        ImGui::Separator();

        ImGui::DragFloat("视口缩放", &m_view_scale, 0.1f, 0.5f, 50.0f, "%.1f");
        ImGui::SliderInt("采样点数", &m_sample_count, 100, 5000);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "滚轮缩放 | 右键拖动平移");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        // 鼠标诊断
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("鼠标: (%.0f, %.0f)  按键: L=%d R=%d",
            io.MousePos.x, io.MousePos.y,
            io.MouseDown[0], io.MouseDown[1]);
        ImGui::Text("WantCapture: M=%d K=%d",
            io.WantCaptureMouse, io.WantCaptureKeyboard);

        // 诊断：输出 draw data 数量（前十帧）
        static int frame_count = 0;
        if(frame_count < 10)
        {
            frame_count++;
            // 在面板里显示
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                "--> 如果你看到这行黄色文字，ImGui 工作正常！");
        }

        if(ImGui::Button("测试点击"))
            std::cout << "[FunctionGraph] 按钮被点击了！\n";

        ImGui::End();
    }
};
