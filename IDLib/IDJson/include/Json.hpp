#pragma once

#include "JsonType.hpp"
#include "ArenaManager.hpp"

namespace ID
{
    class Json
    {
        using Array = std::vector<Json>;
        using Object = std::unordered_map<std::string, Json>;

    public:
        Json() : m_ptr(nullptr), m_type(JSON::Type::Null) {}
        ~Json() = default;      // Json 不负责释放内存

        // 允许浅拷贝与移动语义
        Json(const Json&) = default;
        Json& operator=(const Json&) = default;
        Json(Json&&) noexcept = default;
        Json& operator=(Json&&) noexcept = default;

        explicit Json(bool value);
        explicit Json(int32_t value);
        explicit Json(double value);

        static Json create_string(const char* value, ArenaID arena_id);
        static Json create_string(const std::string& value, ArenaID arena_id);
        static Json create_array(ArenaID arena_id);
        static Json create_object(ArenaID arena_id);

    public:
        JSON::Type  type()      const { return m_type; }

        bool        is_null()   const { return m_type == JSON::Type::Null; }
        bool        is_bool()   const { return m_type == JSON::Type::Bool; }
        bool        is_int()    const { return m_type == JSON::Type::Int; }
        bool        is_float()  const { return m_type == JSON::Type::Float; }
        bool        is_sstr()   const { return m_type == JSON::Type::ShortString; }
        bool        is_string() const { return m_type == JSON::Type::String; }
        bool        is_array()  const { return m_type == JSON::Type::Array; }
        bool        is_object() const { return m_type == JSON::Type::Object; }

    public:
        bool                as_bool()   const;
        int32_t             as_int()    const;
        double              as_float()  const;
        const char*         as_cstr()   const;
        const std::string&  as_string() const;

    public:
        // 对于 JSON::Type::Array 类型，允许通过下标访问元素
        Json&       operator[](size_t index);
        const Json& operator[](size_t index) const;

        void        push_back(const Json& value);
        size_t      size() const;

        // 对于 JSON::Type::Object 类型，允许通过键访问元素
        Json&       operator[](const std::string& key);
        const Json& operator[](const std::string& key) const;

        void insert(const std::string& key, const Json& value);

    private:
        void*       m_ptr = nullptr;
        JSON::Type  m_type = JSON::Type::Null;
    };

    namespace JSON
    {
        inline const Json null{};
    }
} // namespace ID