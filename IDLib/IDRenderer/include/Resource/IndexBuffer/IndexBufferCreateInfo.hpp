#pragma once

#include "Core/IDRpch.hpp"

namespace ID
{
    struct IDR_API IndexBufferCreateInfo
    {
        IndexBufferCreateInfo() = delete;               // 空的 IndexBufferCreateInfo 没有意义
        IndexBufferCreateInfo(const uint32_t* data, uint32_t count) : index_data(data), index_count(count) {}

        const uint32_t* index_data = nullptr;          // 索引数据
        uint32_t        index_count = 0;               // 索引数量
    };
} // namespace ID