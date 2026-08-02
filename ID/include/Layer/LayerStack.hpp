#pragma once

#include "IDpch.hpp"

#include "Layer.hpp"

#include <vector>
#include <memory>

namespace ID
{
    // =====================================================================
    //  LayerStack  — 有序的 Layer 栈
    //
    //      push_layer:   压入栈顶（最后渲染/最先处理事件）
    //      push_overlay: 压入覆盖层（始终在栈顶之上）
    //      pop_layer:    移除指定层
    // =====================================================================
    class ID_API LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        void push_layer(Layer* layer);
        void push_overlay(Layer* overlay);
        void pop_layer(Layer* layer);
        void pop_overlay(Layer* overlay);

        // 迭代器（用于主循环遍历）
        using iterator       = std::vector<Layer*>::iterator;
        using const_iterator = std::vector<Layer*>::const_iterator;

        iterator       begin()          { return m_layers.begin(); }
        iterator       end()            { return m_layers.end(); }
        const_iterator begin() const    { return m_layers.begin(); }
        const_iterator end()   const    { return m_layers.end(); }

    private:
        std::vector<Layer*> m_layers;
        // 栈中普通层与覆盖层的分界索引
        uint32_t m_layer_insert_index = 0;
    };
} // namespace ID
