#pragma once

#include "IDWindow.hpp"

namespace ID
{
    /*
    *   EventDispatcher 类用于分发事件，根据事件类型调用相应的处理函数。
    */
    class EventDispatcher
    {
    public:
        /*
        *   构造 EventDispatcher。
        *   @param event 要分发的事件引用
        */
        EventDispatcher(Event& event) : m_event(event) { }

        /*
        *   尝试将事件分发给回调函数。
        *   @tparam T 目标事件类型
        *   @tparam F 回调函数类型
        *   @param func 回调函数，接受 T& 参数并返回 bool 表示是否处理
        */
        template<typename T, typename F>
        void dispatch(const F& func)
        {
            if(m_event.get_type() == T::get_static_type())
            {
                m_event.set_handled(func(static_cast<T&>(m_event)));
            }
        }

    private:
        Event& m_event;
    };
}