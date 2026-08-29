#pragma once

#include "IDpch.hpp"

#include "imgui.h"

#include "Renderer/Render/RenderGraph/RGTypes.hpp"

namespace ax
{
    namespace NodeEditor
    {
        struct EditorContext;   // 前置声明：成员仅存指针，完整类型与生命周期在 .cpp
    }
} // namespace ax::NodeEditor

namespace ID
{
    /*
    *   RenderGraphEditorWidget：RenderGraph 节点图控件（基于 imgui-node-editor）。
    *   只读可视化 + 管线开关调控：实节点 = 图中 Pass，ghost 节点 = 可装配未启用的 Pass。
    *   依赖声明（setup）自动推导边，本控件不提供连线编辑（见计划书 D1）。
    *
    *   使用方式（D3 解耦）：Panel 每帧 set_graph_view() 注入最新快照 → render() 绘制；
    *   set_ghost_nodes() / set_toggle_callback() 由所属 Panel 组装。
    *   本控件不直接触碰 Renderer，交互仅发出 (key, enable) 回调。
    *
    *   ID 编码（确定性，重建无需映射表）：
    *   NodeId = pass 索引 + 1；LinkId = 边序号 + 1；
    *   PinId = (pass 索引 * 16 + pin 序号) * 2 + (输出 pin ? 1 : 0) + 1；
    *   ghost 节点 NodeId 从 0x10000000 起（与实节点区分）。
    */
    class ID_API RenderGraphEditorWidget
    {
    public:
        RenderGraphEditorWidget();
        ~RenderGraphEditorWidget();

        // 禁止拷贝/移动（持有 EditorContext 裸资源）
        RenderGraphEditorWidget(const RenderGraphEditorWidget&) = delete;
        RenderGraphEditorWidget& operator=(const RenderGraphEditorWidget&) = delete;

        /*
        *   ghost 节点描述：name 为 Pass 显示名，key 用于回调标识。
        *   列表同时充当"可调控 Pass 元数据"：图中已存在的 Pass 依 name 匹配到 key
        *   （节点头部显示 Disable 按钮）；未装配的显示为 ghost 节点（Enable 按钮）。
        *   ForwardPass 不在列表中 → 常开不可禁用。
        */
        struct GhostNode
        {
            std::string name;      // 如 "ShadowPass"
            std::string key;       // 如 "shadow" / "skybox" / "post_process"
            std::string tooltip;   // ghost 悬停提示
        };

        /** 每帧调用：title 为画布区域标签，height 为画布高度 */
        void render(const char* title, float height);

        /** 由 Panel 注入数据：图快照 + ghost 列表 + 启停回调（key, enable） */
        void set_graph_view(const RGGraphView& view);
        void set_ghost_nodes(std::vector<GhostNode> ghosts);
        void set_toggle_callback(std::function<void(const std::string& key, bool enable)> callback);

    private:
        void rebuild_if_changed();   // D6：快照有差异才重建节点布局
        void draw_hover_tooltip();   // 悬停节点信息提示
        void persist_positions();    // 节点位置落盘（变化时写入，自控持久化）

        // 实现细节（EditorContext 生命周期、变更检测、rebuild、绘制）全部在 .cpp
        ax::NodeEditor::EditorContext*                m_context = nullptr;   // 编辑器上下文（构造创建 / 析构销毁）
        RGGraphView                                   m_view;                // 最近一次注入的图快照
        std::vector<GhostNode>                        m_ghosts;              // 可调控 Pass 元数据（含 ghost）
        std::function<void(const std::string&, bool)> m_toggle_callback;    // 启停回调（key, enable）
        std::string                                   m_last_signature;     // 上次重建时的快照签名（D6 变更检测）
        bool                                          m_needs_rebuild = true; // 强制重建标记（ghost 列表变化等）
        std::map<std::string, ImVec2>                 m_saved_positions;    // 节点 key（"pass:名" / "ghost:名"）→ 保存的位置
        std::map<std::string, ImVec2>                 m_last_persisted;     // 上次落盘的位置快照（变化检测）
    };
} // namespace ID
