#pragma once

#include "Core/IDRpch.hpp"

namespace ID
{
    enum class AttributeType : uint8_t
    {
        Float   = 0,
        Float2  = 1,
        Float3  = 2,
        Float4  = 3,
        Int     = 4,
        Int2    = 5,
        Int3    = 6,
        Int4    = 7,
        UByte4  = 8,
    };

    struct VertexBufferAttribute
    {
        std::string         name;
        AttributeType       type;
        bool                normalized = false;
        uint32_t            offset = 0;
    };
} // namespace ID