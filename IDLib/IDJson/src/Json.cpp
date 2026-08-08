#include "Json.hpp"
#include "Arena.hpp"
#include "ArenaAllocator.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

namespace ID
{
    // ═══════════════════════════════════════════
    //  内部真实类型 — 使用 ArenaAllocator 接管 STL 容器
    //
    //  Json.hpp 中 private 的 using Array / using Object
    //  仅为标注目的，实际 m_ptr 指向的是这里的类型。
    //  这样 ArenaAllocator 完全不暴露给公开头文件。
    // ═══════════════════════════════════════════
    using ArrayImpl  = std::vector<Json, ArenaAllocator<Json>>;
    using ObjectImpl = std::unordered_map<
        std::string,
        Json,
        std::hash<std::string>,
        std::equal_to<>,
        ArenaAllocator<std::pair<const std::string, Json>>
    >;

    // ═══════════════════════════════════════════
    //  标量构造（零分配）
    // ═══════════════════════════════════════════

    Json::Json(bool value)
        : m_ptr(reinterpret_cast<void*>(static_cast<uintptr_t>(value ? 1 : 0)))
        , m_type(JSON::Type::Bool)
    {}

    Json::Json(int32_t value)
        : m_ptr(reinterpret_cast<void*>(static_cast<intptr_t>(static_cast<int64_t>(value))))
        , m_type(JSON::Type::Int)
    {}

    Json::Json(double value)
        : m_type(JSON::Type::Float)
    {
        static_assert(sizeof(double) <= sizeof(void*),
                      "double must fit in void* for tagged pointer storage");
        std::memcpy(&m_ptr, &value, sizeof(value));
    }

    // ═══════════════════════════════════════════
    //  静态工厂方法
    // ═══════════════════════════════════════════

    Json Json::create_string(const char* value, ArenaID arena_id)
    {
        size_t len = std::strlen(value);

        // ≤ 7 字节 → ShortString，直接 inline 进 m_ptr 的 8 字节空间
        if (len <= 7)
        {
            Json j;
            j.m_type = JSON::Type::ShortString;
            char* dst = reinterpret_cast<char*>(&j.m_ptr);
            std::memcpy(dst, value, len);
            dst[len] = '\0';
            return j;
        }

        // > 7 字节 → 在 Arena 上构造 std::string
        Json j;
        j.m_type = JSON::Type::String;
        j.m_ptr  = get_arena(arena_id).create<std::string>(value);
        return j;
    }

    Json Json::create_string(const std::string& value, ArenaID arena_id)
    {
        if (value.size() <= 7)
        {
            Json j;
            j.m_type = JSON::Type::ShortString;
            char* dst = reinterpret_cast<char*>(&j.m_ptr);
            std::memcpy(dst, value.data(), value.size());
            dst[value.size()] = '\0';
            return j;
        }

        Json j;
        j.m_type = JSON::Type::String;
        j.m_ptr  = get_arena(arena_id).create<std::string>(value);
        return j;
    }

    Json Json::create_array(ArenaID arena_id)
    {
        auto& arena = get_arena(arena_id);

        Json j;
        j.m_type = JSON::Type::Array;
        // 关键：vector 构造时绑定 ArenaAllocator，
        // 后续 push_back 扩容全部走 arena.allocate() → bump pointer
        j.m_ptr = arena.create<ArrayImpl>(ArenaAllocator<Json>(arena));
        return j;
    }

    Json Json::create_object(ArenaID arena_id)
    {
        auto& arena = get_arena(arena_id);

        Json j;
        j.m_type = JSON::Type::Object;
        j.m_ptr = arena.create<ObjectImpl>(
            ArenaAllocator<std::pair<const std::string, Json>>(arena));
        return j;
    }

    // ═══════════════════════════════════════════
    //  值访问
    // ═══════════════════════════════════════════

    bool Json::as_bool() const
    {
        assert(m_type == JSON::Type::Bool);
        return m_ptr != nullptr;
    }

    int32_t Json::as_int() const
    {
        // 兼容 Float：JSON 中 "1.0" 等带小数写法读整型时安全截断
        if (m_type == JSON::Type::Float)
        {
            return static_cast<int32_t>(as_float());
        }

        assert(m_type == JSON::Type::Int);
        return static_cast<int32_t>(reinterpret_cast<intptr_t>(m_ptr));
    }

    double Json::as_float() const
    {
        // 兼容 Int：旧文件可能把整数值浮点写成 "1"（JsonParser 解析为 Int）
        if (m_type == JSON::Type::Int)
        {
            return static_cast<double>(as_int());
        }

        assert(m_type == JSON::Type::Float);
        double d;
        std::memcpy(&d, &m_ptr, sizeof(d));
        return d;
    }

    const char* Json::as_cstr() const
    {
        if (m_type == JSON::Type::ShortString)
        {
            // ShortString 的字符直接存在 &m_ptr 的 8 字节空间里
            return reinterpret_cast<const char*>(&m_ptr);
        }

        assert(m_type == JSON::Type::String);
        return static_cast<const std::string*>(m_ptr)->c_str();
    }

    const std::string& Json::as_string() const
    {
        assert(m_type == JSON::Type::String || m_type == JSON::Type::ShortString);
        if(m_type == JSON::Type::ShortString)
        {
            return *reinterpret_cast<const std::string*>(&m_ptr);
        }
        else
        {
            return *static_cast<const std::string*>(m_ptr);
        }
        
    }

    // ═══════════════════════════════════════════
    //  Array 元素访问
    // ═══════════════════════════════════════════

    Json& Json::operator[](size_t index)
    {
        assert(m_type == JSON::Type::Array);
        return (*static_cast<ArrayImpl*>(m_ptr))[index];
    }

    const Json& Json::operator[](size_t index) const
    {
        assert(m_type == JSON::Type::Array);
        return (*static_cast<const ArrayImpl*>(m_ptr))[index];
    }

    void Json::push_back(const Json& value)
    {
        assert(m_type == JSON::Type::Array);
        // ArrayImpl 构造时已绑定 ArenaAllocator，扩容自动走 bump pointer
        static_cast<ArrayImpl*>(m_ptr)->push_back(value);
    }

    // ═══════════════════════════════════════════
    //  Object 元素访问
    // ═══════════════════════════════════════════

    Json& Json::operator[](const std::string& key)
    {
        assert(m_type == JSON::Type::Object);
        // unordered_map::operator[]: key 不存在时自动插入默认 Json
        return (*static_cast<ObjectImpl*>(m_ptr))[key];
    }

    const Json& Json::operator[](const std::string& key) const
    {
        assert(m_type == JSON::Type::Object);
        // const 版本必须用 at(): key 不存在时抛 std::out_of_range
        return static_cast<const ObjectImpl*>(m_ptr)->at(key);
    }

    void Json::insert(const std::string& key, const Json& value)
    {
        assert(m_type == JSON::Type::Object);
        // insert_or_assign: 存在则更新，不存在则插入
        static_cast<ObjectImpl*>(m_ptr)->insert_or_assign(key, value);
    }

    bool Json::contains(const std::string& key) const
    {
        if (m_type != JSON::Type::Object)
        {
            return false;
        }
        const auto& obj = *static_cast<const ObjectImpl*>(m_ptr);
        return obj.find(key) != obj.end();
    }

    std::vector<std::string> Json::get_keys() const
    {
        assert(m_type == JSON::Type::Object);
        std::vector<std::string> keys;
        const auto& obj = *static_cast<const ObjectImpl*>(m_ptr);
        for (const auto& [k, v] : obj)
            keys.push_back(k);
        return keys;
    }

    // ═══════════════════════════════════════════
    //  容量查询
    // ═══════════════════════════════════════════

    size_t Json::size() const
    {
        switch (m_type)
        {
        case JSON::Type::Array:
            return static_cast<const ArrayImpl*>(m_ptr)->size();
        case JSON::Type::Object:
            return static_cast<const ObjectImpl*>(m_ptr)->size();
        case JSON::Type::String:
            return static_cast<const std::string*>(m_ptr)->size();
        case JSON::Type::ShortString:
            return std::strlen(reinterpret_cast<const char*>(&m_ptr));
        default:
            return 0;
        }
    }

} // namespace ID
