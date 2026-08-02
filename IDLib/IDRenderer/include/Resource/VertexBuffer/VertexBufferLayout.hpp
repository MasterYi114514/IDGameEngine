#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/VertexBuffer/VertexBufferAttribute.hpp"

namespace ID
{
    template<typename... VAttrs>
    concept AllVertexBufferAttributes = (std::same_as<VAttrs, VertexBufferAttribute> && ...);

    /*
    *   VertexBufferLayout 类用于描述顶点数据的布局信息
    *   注意：每次修改 attributes 后都需要调用 calculate_stride() 来更新 m_stride
    */
    class IDR_API VertexBufferLayout
    {
    public:
        VertexBufferLayout() = default;
        VertexBufferLayout(std::initializer_list<VertexBufferAttribute> attributes)
            : m_attributes(attributes) { calculate_stride(); }
        VertexBufferLayout(const std::vector<VertexBufferAttribute>& attributes)
            : m_attributes(attributes) { calculate_stride(); }
        ~VertexBufferLayout() = default;

        /*
        *   通过可变长参数模版万能引用的方式，接收任意数量的 VertexAttribute 对象，并将它们添加到 m_attributes 中
        */
        template<typename... VAttrs>
        requires AllVertexBufferAttributes<VAttrs...>
        void push(VAttrs&&... attributes)
        {
            (emplace_back(std::forward<VAttrs>(attributes)), ...);
            calculate_stride();
        }

        void clear() { m_attributes.clear(); m_stride = 0; }

    public:
        // 供外界访问的只读接口
        uint32_t get_stride() const { return m_stride; }
        size_t get_attribute_count() const { return m_attributes.size(); }
        const VertexBufferAttribute& operator[](size_t index) const { return m_attributes[index]; }

    private:
        std::vector<VertexBufferAttribute>      m_attributes;
        uint32_t                                m_stride = 0;

        /*
        *   计算当前 attributes 的 stride，并更新 m_stride
        *   每次修改 attributes 后都需要调用此函数
        */
        void calculate_stride();

        /*
        *   将一个 VertexAttribute 对象添加到 m_attributes 中
        *   注意：此函数不会更新 m_stride，需要在调用后手动调用 calculate_stride()
        */
        void emplace_back(VertexBufferAttribute&& attribute);

    public:
        // 迭代器的支持，仅提供 const 版本的迭代器
        using const_iterator = std::vector<VertexBufferAttribute>::const_iterator;
        const_iterator begin() const { return m_attributes.begin(); }
        const_iterator end() const { return m_attributes.end(); }
    };
} // namespace ID