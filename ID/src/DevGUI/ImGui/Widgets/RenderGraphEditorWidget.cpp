#include "DevGUI/ImGui/Widgets/RenderGraphEditorWidget.hpp"

#include <algorithm>
#include <cfloat>
#include <fstream>

#include "imgui_node_editor.h"

namespace
{
    using ID::RGEdgeType;
    using ID::RGEdgeView;
    using ID::RGPassView;
    using ID::RGGraphView;
    using ID::RGResource;
    using ID::RenderGraphEditorWidget;

    /*
    *   make_signature：快照签名（pass 名 + 执行序 + culled + 边），D6 变更检测用
    */
    std::string make_signature(const RGGraphView& view)
    {
        std::string sig;
        for(const RGPassView& pv : view.passes)
        {
            sig += pv.name;
            sig += '#';
            sig += std::to_string(pv.exec_index);
            sig += pv.culled ? 'C' : '.';
            sig += ';';
        }
        for(const RGEdgeView& e : view.edges)
        {
            sig += std::to_string(e.from);
            sig += '>';
            sig += std::to_string(e.to);
            sig += ':';
            sig += std::to_string(static_cast<int>(e.type));
            sig += ';';
        }
        return sig;
    }

    /*
    *   make_pin_id：PinId 编码（计划书 Step 7）
    *   serial：reads 用 0..7，writes 用 8..15
    */
    uintptr_t make_pin_id(uint32_t pass_index, uint32_t serial, bool is_output)
    {
        return (static_cast<uintptr_t>(pass_index) * 16u + serial) * 2u + (is_output ? 1u : 0u) + 1u;
    }

    /*
    *   edge_type_tag：边类型 → 短标签（tooltip 用）
    */
    const char* edge_type_tag(RGEdgeType type)
    {
        switch(type)
        {
            case RGEdgeType::RAW:      return "RAW";
            case RGEdgeType::WAW:      return "WAW";
            case RGEdgeType::WAR:      return "WAR";
            case RGEdgeType::Order:    return "Order";
            case RGEdgeType::Explicit: return "Explicit";
        }
        return "?";
    }

    /*
    *   resource_color / edge_color：Pin 与边着色（资源三色 / 边类型五色）
    */
    ImVec4 resource_color(RGResource res)
    {
        switch(res)
        {
            case RGResource::ShadowMap:      return ImVec4(0.35f, 0.50f, 0.95f, 1.0f);   // 蓝：阴影贴图
            case RGResource::SceneColor:     return ImVec4(0.95f, 0.72f, 0.25f, 1.0f);   // 橙：场景颜色
            case RGResource::ViewportTarget: return ImVec4(0.35f, 0.85f, 0.45f, 1.0f);   // 绿：视口目标
            default:                         return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        }
    }

    ImVec4 edge_color(RGEdgeType type)
    {
        switch(type)
        {
            case RGEdgeType::RAW:      return ImVec4(0.75f, 0.82f, 1.00f, 1.0f);   // 白蓝：读写依赖
            case RGEdgeType::WAW:      return ImVec4(1.00f, 0.42f, 0.42f, 1.0f);   // 红：写写保序
            case RGEdgeType::WAR:      return ImVec4(1.00f, 0.65f, 0.30f, 1.0f);   // 橙：写读冲突
            case RGEdgeType::Order:    return ImVec4(0.60f, 0.60f, 0.68f, 1.0f);   // 灰：读读保序
            case RGEdgeType::Explicit: return ImVec4(0.75f, 0.50f, 1.00f, 1.0f);   // 紫：显式 after()
            default:                   return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        }
    }

    /*
    *   resource_name：槽位名（"ShadowMap" 等），越界兜底 "?"
    */
    const char* resource_name(const RGGraphView& view, RGResource res)
    {
        const size_t idx = static_cast<size_t>(res);
        if(idx < view.resource_names.size()) return view.resource_names[idx].c_str();
        return "?";
    }

    /*
    *   find_ghost_key：pass 名 → 调控 key（未登记 = nullptr，ForwardPass 由此不可禁用）
    */
    const std::string* find_ghost_key(const std::vector<RenderGraphEditorWidget::GhostNode>& ghosts,
        const std::string& name)
    {
        for(const RenderGraphEditorWidget::GhostNode& g : ghosts)
        {
            if(g.name == name) return &g.key;
        }
        return nullptr;
    }

    /*
    *   pass_exists：图中是否已存在该 pass（存在 → 画实节点，否则画 ghost）
    */
    bool pass_exists(const RGGraphView& view, const std::string& name)
    {
        for(const RGPassView& pv : view.passes)
        {
            if(pv.name == name) return true;
        }
        return false;
    }

    /*
    *   resolve_link_pins：按边类型匹配资源，解析边两端的 pin。
    *   RAW: to.reads ∩ from.writes；WAW: to.writes ∩ from.writes；
    *   WAR: to.writes ∩ from.reads；Order: to.reads ∩ from.reads；
    *   Explicit: 无资源语义，兜底连 from 首个输出 → to 首个输入。
    *   返回 false 表示无匹配 pin（不画该边）。
    */
    bool resolve_link_pins(const RGPassView& from, const RGPassView& to, RGEdgeType type,
        ax::NodeEditor::PinId& from_pin, ax::NodeEditor::PinId& to_pin)
    {
        if(type == RGEdgeType::Explicit)
        {
            if(from.writes.empty() || to.reads.empty()) return false;
            from_pin = ax::NodeEditor::PinId(make_pin_id(from.index, 8u, true));
            to_pin   = ax::NodeEditor::PinId(make_pin_id(to.index, 0u, false));
            return true;
        }

        const std::vector<RGResource>* to_res   = (type == RGEdgeType::RAW || type == RGEdgeType::Order)
            ? &to.reads : &to.writes;
        const std::vector<RGResource>* from_res = (type == RGEdgeType::RAW || type == RGEdgeType::WAW)
            ? &from.writes : &from.reads;

        for(size_t ti = 0; ti < to_res->size(); ++ti)
        {
            for(size_t fi = 0; fi < from_res->size(); ++fi)
            {
                if((*to_res)[ti] != (*from_res)[fi]) continue;

                const bool to_is_read   = (type == RGEdgeType::RAW || type == RGEdgeType::Order);
                const bool from_is_write = (type == RGEdgeType::RAW || type == RGEdgeType::WAW);
                const uint32_t from_serial = from_is_write ? 8u + static_cast<uint32_t>(fi)
                    : static_cast<uint32_t>(fi);
                const uint32_t to_serial = to_is_read ? static_cast<uint32_t>(ti)
                    : 8u + static_cast<uint32_t>(ti);
                from_pin = ax::NodeEditor::PinId(make_pin_id(from.index, from_serial, true));
                to_pin   = ax::NodeEditor::PinId(make_pin_id(to.index, to_serial, false));
                return true;
            }
        }
        return false;
    }

    /*
    *   draw_legend：画布顶部图例（资源三色 + 边五色）
    */
    void draw_legend(const RGGraphView& view)
    {
        ImGui::PushID("rg_legend");
        ImGui::TextUnformatted("资源:");
        for(int r = 0; r < static_cast<int>(RGResource::Count); ++r)
        {
            ImGui::SameLine();
            ImGui::PushID(r);
            ImGui::ColorButton("", resource_color(static_cast<RGResource>(r)),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(10.0f, 10.0f));
            ImGui::SameLine();
            ImGui::TextUnformatted(resource_name(view, static_cast<RGResource>(r)));
            ImGui::PopID();
        }
        ImGui::TextUnformatted("边:");
        ImGui::SameLine();
        const RGEdgeType types[] = { RGEdgeType::RAW, RGEdgeType::WAW, RGEdgeType::WAR,
            RGEdgeType::Order, RGEdgeType::Explicit };
        const char* type_names[] = { "RAW", "WAW", "WAR", "Order", "Explicit" };
        for(size_t t = 0; t < 5; ++t)
        {
            ImGui::PushID(100 + static_cast<int>(t));
            ImGui::ColorButton("", edge_color(types[t]),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(10.0f, 10.0f));
            ImGui::SameLine();
            ImGui::TextUnformatted(type_names[t]);
            ImGui::PopID();
            if(t < 4) ImGui::SameLine();
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    /*
    *   布局持久化（自控方案，不依赖 node editor 内部 SaveSettings/LoadSettings 机制）：
    *   位置文件为行式文本（每行 "key x y"，key = "pass:名" / "ghost:名"），
    *   存放于工作目录 RenderGraphEditor.layout（与 imgui.ini 同处）。
    *   读取失败 / 文件不存在 → 返回空表（全部用默认布局）。
    */
    std::string settings_path()
    {
        return std::filesystem::absolute("RenderGraphEditor.layout").string();
    }

    std::map<std::string, ImVec2> load_positions()
    {
        std::map<std::string, ImVec2> positions;
        std::ifstream file(settings_path(), std::ios::binary);
        if(!file.is_open()) return positions;
        std::string key;
        float x = 0.0f;
        float y = 0.0f;
        while(file >> key >> x >> y)
        {
            positions[key] = ImVec2(x, y);
        }
        return positions;
    }

    void save_positions(const std::map<std::string, ImVec2>& positions)
    {
        std::ofstream file(settings_path(), std::ios::binary);
        if(!file.is_open()) return;
        for(const auto& [key, pos] : positions)
        {
            file << key << ' ' << pos.x << ' ' << pos.y << '\n';
        }
    }

    /*
    *   same_positions：两份位置表逐项比较（ImVec2 无 operator== 依赖，显式比较）
    */
    bool same_positions(const std::map<std::string, ImVec2>& a, const std::map<std::string, ImVec2>& b)
    {
        if(a.size() != b.size()) return false;
        for(const auto& [key, pos] : a)
        {
            const auto it = b.find(key);
            if(it == b.end()) return false;
            if(it->second.x != pos.x || it->second.y != pos.y) return false;
        }
        return true;
    }
} // 匿名命名空间

namespace ID
{
    RenderGraphEditorWidget::RenderGraphEditorWidget()
    {
        ax::NodeEditor::Config config;
        config.SettingsFile     = nullptr;   // 禁用 node editor 内置文件 IO（布局由本控件自控持久化）
        config.EnableSmoothZoom = true;      // 平滑缩放（体验打磨 Step 10）
        m_context = ax::NodeEditor::CreateEditor(&config);
        m_saved_positions = load_positions();   // 读取上次保存的节点位置
    }

    RenderGraphEditorWidget::~RenderGraphEditorWidget()
    {
        if(m_context)
        {
            ax::NodeEditor::DestroyEditor(m_context);
            m_context = nullptr;
        }
    }

    void RenderGraphEditorWidget::set_graph_view(const RGGraphView& view)
    {
        m_view = view;
    }

    void RenderGraphEditorWidget::set_ghost_nodes(std::vector<GhostNode> ghosts)
    {
        m_ghosts = std::move(ghosts);
        m_needs_rebuild = true;   // ghost 布局变化，下一帧重建
    }

    void RenderGraphEditorWidget::set_toggle_callback(
        std::function<void(const std::string& key, bool enable)> callback)
    {
        m_toggle_callback = std::move(callback);
    }

    void RenderGraphEditorWidget::render(const char* title, float height)
    {
        ax::NodeEditor::SetCurrentEditor(m_context);   // SetNodePosition 等 API 需要当前上下文
        rebuild_if_changed();

        draw_legend(m_view);

        ax::NodeEditor::Begin(title, ImVec2(0.0f, height));
        {
            // ——— 实节点：图中全部 Pass（culled 灰显）———
            for(size_t i = 0; i < m_view.passes.size(); ++i)
            {
                const RGPassView& pv = m_view.passes[i];
                const ax::NodeEditor::NodeId node_id(static_cast<uintptr_t>(i + 1u));

                if(pv.culled)
                {
                    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBg, ImColor(42, 42, 46, 190));
                    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBorder, ImColor(110, 110, 115, 130));
                }

                ax::NodeEditor::BeginNode(node_id);

                // 头部：pass 名 +（可禁用时）× 按钮（ForwardPass 不在 ghost 列表 → 无 key → 无按钮）
                ImGui::TextUnformatted(pv.name.c_str());
                const std::string* key = find_ghost_key(m_ghosts, pv.name);
                if(key)
                {
                    ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(i));
                    if(ImGui::SmallButton("x"))
                    {
                        if(m_toggle_callback) m_toggle_callback(*key, false);
                    }
                    ImGui::PopID();
                }

                // 左侧 reads pin（输入）
                for(size_t r = 0; r < pv.reads.size(); ++r)
                {
                    const ax::NodeEditor::PinId pin_id(make_pin_id(static_cast<uint32_t>(i),
                        static_cast<uint32_t>(r), false));
                    ax::NodeEditor::BeginPin(pin_id, ax::NodeEditor::PinKind::Input);
                    ImGui::TextColored(resource_color(pv.reads[r]), "%s", resource_name(m_view, pv.reads[r]));
                    ax::NodeEditor::EndPin();
                }
                // 右侧 writes pin（输出）
                for(size_t w = 0; w < pv.writes.size(); ++w)
                {
                    const ax::NodeEditor::PinId pin_id(make_pin_id(static_cast<uint32_t>(i),
                        8u + static_cast<uint32_t>(w), true));
                    ax::NodeEditor::BeginPin(pin_id, ax::NodeEditor::PinKind::Output);
                    ImGui::TextColored(resource_color(pv.writes[w]), "%s", resource_name(m_view, pv.writes[w]));
                    ax::NodeEditor::EndPin();
                }

                ax::NodeEditor::EndNode();

                if(pv.culled)
                {
                    ax::NodeEditor::PopStyleColor(2);
                }
            }

            // ——— ghost 节点：可装配但当前未启用（低透明度 + Enable 按钮）———
            for(size_t g = 0; g < m_ghosts.size(); ++g)
            {
                const GhostNode& ghost = m_ghosts[g];
                if(pass_exists(m_view, ghost.name)) continue;

                const ax::NodeEditor::NodeId node_id(static_cast<uintptr_t>(0x10000000u + g + 1u));
                ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBg, ImColor(32, 32, 32, 110));
                ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBorder, ImColor(255, 255, 255, 60));
                ax::NodeEditor::BeginNode(node_id);
                ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", ghost.name.c_str());
                ImGui::SameLine();
                // 多个 ghost 并存时按钮 ID 冲突，需 PushID 隔离（与实节点 x 按钮一致）
                ImGui::PushID(static_cast<int>(0x10000000u + g));
                if(ImGui::SmallButton("Enable"))
                {
                    if(m_toggle_callback) m_toggle_callback(ghost.key, true);
                }
                ImGui::PopID();
                ax::NodeEditor::EndNode();
                ax::NodeEditor::PopStyleColor(2);
            }

            // ——— 边：按边类型着色 ———
            for(size_t ei = 0; ei < m_view.edges.size(); ++ei)
            {
                const RGEdgeView& e = m_view.edges[ei];
                if(e.from >= m_view.passes.size() || e.to >= m_view.passes.size()) continue;
                const RGPassView& from = m_view.passes[e.from];
                const RGPassView& to   = m_view.passes[e.to];

                ax::NodeEditor::PinId from_pin;
                ax::NodeEditor::PinId to_pin;
                if(!resolve_link_pins(from, to, e.type, from_pin, to_pin)) continue;
                ax::NodeEditor::Link(ax::NodeEditor::LinkId(static_cast<uintptr_t>(ei + 1u)),
                    from_pin, to_pin, edge_color(e.type), 2.0f);
            }
        }
        ax::NodeEditor::End();

        draw_hover_tooltip();
        persist_positions();
    }

    void RenderGraphEditorWidget::persist_positions()
    {
        // 收集全部节点当前位置（未提交的节点 GetNodePosition 返回 FLT_MAX，跳过）
        std::map<std::string, ImVec2> current;
        for(size_t i = 0; i < m_view.passes.size(); ++i)
        {
            const ImVec2 pos = ax::NodeEditor::GetNodePosition(
                ax::NodeEditor::NodeId(static_cast<uintptr_t>(i + 1u)));
            if(pos.x == FLT_MAX) continue;
            current["pass:" + m_view.passes[i].name] = pos;
        }
        for(size_t g = 0; g < m_ghosts.size(); ++g)
        {
            const ImVec2 pos = ax::NodeEditor::GetNodePosition(
                ax::NodeEditor::NodeId(static_cast<uintptr_t>(0x10000000u + g + 1u)));
            if(pos.x == FLT_MAX) continue;
            current["ghost:" + m_ghosts[g].name] = pos;
        }

        // 有变化才落盘（拖动过程中每帧更新一次，文件极小无压力）
        if(same_positions(current, m_last_persisted)) return;
        m_last_persisted = current;
        m_saved_positions = current;
        save_positions(current);
    }

    void RenderGraphEditorWidget::rebuild_if_changed()
    {
        const std::string sig = make_signature(m_view);
        if(!m_needs_rebuild && sig == m_last_signature) return;
        m_needs_rebuild = false;
        m_last_signature = sig;

        // 布局语义：优先使用保存的位置（重启恢复 / 保留用户拖动）；
        // 未保存过的新节点（开关切换新增）用默认布局（实节点横向一行 / culled 最右灰区 / ghost 下方一行）。
        float max_y        = 0.0f;
        float culled_max_y = -170.0f;
        const float culled_x = (static_cast<float>(m_view.passes.size()) + 1.0f) * 320.0f + 200.0f;

        for(size_t i = 0; i < m_view.passes.size(); ++i)
        {
            const RGPassView& pv = m_view.passes[i];
            const ax::NodeEditor::NodeId node_id(static_cast<uintptr_t>(i + 1u));

            ImVec2 pos;
            const auto saved = m_saved_positions.find("pass:" + pv.name);
            if(saved != m_saved_positions.end())
            {
                pos = saved->second;
            }
            else
            {
                if(pv.exec_index >= 0)
                {
                    pos = ImVec2(pv.exec_index * 320.0f, 0.0f);
                }
                else
                {
                    culled_max_y += 170.0f;
                    pos = ImVec2(culled_x, culled_max_y);
                }
            }
            if(pv.exec_index < 0) culled_max_y = std::max(culled_max_y, pos.y);
            max_y = std::max(max_y, pos.y);
            ax::NodeEditor::SetNodePosition(node_id, pos);
        }

        for(size_t g = 0; g < m_ghosts.size(); ++g)
        {
            const GhostNode& ghost = m_ghosts[g];
            if(pass_exists(m_view, ghost.name)) continue;
            const ax::NodeEditor::NodeId node_id(static_cast<uintptr_t>(0x10000000u + g + 1u));

            ImVec2 pos;
            const auto saved = m_saved_positions.find("ghost:" + ghost.name);
            if(saved != m_saved_positions.end())
            {
                pos = saved->second;
            }
            else
            {
                pos = ImVec2(100.0f + static_cast<float>(g) * 320.0f, max_y + 250.0f);
            }
            ax::NodeEditor::SetNodePosition(node_id, pos);
        }
    }

    void RenderGraphEditorWidget::draw_hover_tooltip()
    {
        const ax::NodeEditor::NodeId hovered = ax::NodeEditor::GetHoveredNode();
        if(!hovered) return;
        const uintptr_t raw = hovered.Get();

        // ghost 节点（高位区段）
        if(raw >= 0x10000000u)
        {
            const size_t g = raw - 0x10000000u - 1u;
            if(g >= m_ghosts.size()) return;
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Ghost: 可装配但当前未启用");
            if(!m_ghosts[g].tooltip.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted(m_ghosts[g].tooltip.c_str());
            }
            ImGui::EndTooltip();
            return;
        }

        // 实节点：执行序号 / culled / 读写资源 / 前驱边
        const size_t idx = raw - 1u;
        if(idx >= m_view.passes.size()) return;
        const RGPassView& pv = m_view.passes[idx];

        std::string reads;
        for(size_t r = 0; r < pv.reads.size(); ++r)
        {
            if(r > 0) reads += ", ";
            reads += resource_name(m_view, pv.reads[r]);
        }
        std::string writes;
        for(size_t w = 0; w < pv.writes.size(); ++w)
        {
            if(w > 0) writes += ", ";
            writes += resource_name(m_view, pv.writes[w]);
        }
        std::string preds;
        bool first = true;
        for(const RGEdgeView& e : m_view.edges)
        {
            if(e.to != idx) continue;
            if(!first) preds += ", ";
            first = false;
            preds += m_view.passes[e.from].name;
            preds += '(';
            preds += edge_type_tag(e.type);
            preds += ')';
        }

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(pv.name.c_str());
        ImGui::Separator();
        ImGui::Text("执行序号: %d", pv.exec_index);
        ImGui::Text("状态: %s", pv.culled ? "culled（已剔除，不执行）" : "active");
        ImGui::Text("读取: %s", reads.empty() ? "无" : reads.c_str());
        ImGui::Text("写入: %s", writes.empty() ? "无" : writes.c_str());
        ImGui::Text("前驱边: %s", preds.empty() ? "无" : preds.c_str());
        ImGui::EndTooltip();
    }
} // namespace ID
