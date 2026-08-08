#pragma once

#include "IDJson.hpp"

namespace ID
{
    /**
     *  可序列化为 Json 文件的基类
     *  所有可序列化的类都应继承自该类，并实现 serialize() 和 deserialize() 方法
     */
    struct SerializableBase
    {
        SerializableBase() = default;
        virtual ~SerializableBase() = default;

        virtual Json serialize(ArenaID arena_id) const = 0;
        virtual void deserialize(const Json& json) = 0;
    };
} // namespace ID