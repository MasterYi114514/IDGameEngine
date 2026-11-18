#include "cmake/Common/Writer.hpp"

#include "cmake/Common/Assert.hpp"

namespace ID::CMake
{
    void Writer::push_line(std::string_view text)
    {
        if (!text.empty())
        {
            // 空文本只写换行：空行不应带前导空格，否则与 push_blank_line 的产物不一致
            m_buffer.append(m_indent * kIndentWidth, ' ');
            m_buffer.append(text);
        }
        m_buffer.push_back('\n');
    }

    void Writer::push_unindented(std::string_view text)
    {
        m_buffer.append(text);
        m_buffer.push_back('\n');
    }

    void Writer::push_blank_line()
    {
        m_buffer.push_back('\n');
    }

    void Writer::begin_scope()
    {
        ++m_indent;
    }

    void Writer::end_scope()
    {
        ID_ASSERT(m_indent > 0);
        --m_indent;
    }

    std::string_view Writer::view() const noexcept
    {
        return std::string_view(m_buffer);
    }

    std::size_t Writer::size() const noexcept
    {
        return m_buffer.size();
    }

    void Writer::reset()
    {
        m_buffer.clear();
        m_indent = 0;
    }
} // namespace ID::CMake
