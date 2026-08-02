#pragma once

#include "IDpch.hpp"

#include <Events/Event.hpp>
#include <Application/Timestep.hpp>

namespace ID
{
    // =====================================================================
    //  Layer  — 游戏状态/功能层的基类
    //
    //      每个 Layer 代表一个独立的游戏模块，可叠加。
    //      示例：MenuLayer、GameplayLayer、HUDLayer
    //
    //      调用顺序：先 Push 的后更新（栈顶最先），
    //      但事件传播相反：最上层 Layer 优先处理，可阻断。
    // =====================================================================
    class ID_API Layer
    {
    public:
        Layer(const std::string& name = "Layer")
            : m_debug_name(name) { }
        virtual ~Layer() = default;

        /**
         *  on_attach()：当 Layer 被添加到 LayerStack 时调用
         *  会执行资源加载、初始化等操作
         */
        virtual void on_attach() { }

        /**
         *  on_detach()：当 Layer 被从 LayerStack 移除时调用
         *  可以执行资源释放、清理等操作
         */
        virtual void on_detach() { }

        // 每帧更新（帧率无关，使用 Timestep）
        virtual void on_update(Timestep ts) { }
        
        /**
         *  on_event()：处理事件
         *  返回 true 表示已处理，阻断传播
         */
        virtual void on_event(Event& event) { }

        const std::string& get_name() const { return m_debug_name; }

    protected:
        std::string m_debug_name;
    };
} // namespace ID
