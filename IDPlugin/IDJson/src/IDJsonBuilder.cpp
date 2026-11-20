#include "IDJsonBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "Arena/SegmentedArena.hpp"

#include "IDJsonAggregate.hpp"

/**
 *  JsonBuilder 的实现。
 *
 *  实现说明（为什么这样写）：
 *  - 存储布局完全复用解析侧的那一套：≤ 7 字节的字符串内联在 JsonValue 里，长字符串写
 *    Detail::StringRef + arena 上的字符数据，数组/对象写 Detail::ArrayRef/ObjectRef + arena 上的
 *    连续块。追加与扩容走 IDJsonAggregate.hpp 的同一份实现，因此构建产物与 parse 的返回值同构。
 *  - 打开中的容器用"帧栈"记录：帧本身在构造/reset() 时从 arena 一次性申请（省掉构建器的固定数组，
 *    一个构建器对象只有几十字节）。帧里存的是描述块地址而非元素地址，数组/对象扩容换块不会让
 *    已打开的帧失效。
 *  - 提交值前先检查"值能不能放"（根值是否已有、对象里是否已给键），因此失败不会先分配后丢弃；
 *    已经分配的块随 reset()/析构统一归还，arena 不回收单块。
 *  - 粘性失败态：首次失败后 root() 立即置空、帧栈清空（与解析失败置空同口径），后续调用一律返回
 *    同一个错误码，调用方不需要每步都检查也不会在半个树上继续追加。
 */

namespace
{
    using ID::Json::Detail::ArrayRef;
    using ID::Json::Detail::ObjectRef;
    using ID::Json::Detail::StringRef;
    using ID::Json::JsonBuildError;
    using ID::Json::JsonType;
    using ID::Json::JsonValue;
    using ID::Json::kInlineStringCapacity;

    // 字符串/键的 32 位长度计量上限：超过即拒绝（内部长度字段是 32 位，超限会回绕）
    ID_FORCEINLINE bool is_text_too_large(const std::size_t size) noexcept
    {
        if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t))
        {
            return size > static_cast<std::size_t>(0xFFFFFFFFu);
        }
        return false;
    }
} // namespace

namespace ID::Json
{
    /**
     *  打开中的容器帧：只记录 arena 上的描述块与对象侧待插入的键。
     *  头里只前向声明，布局留在实现文件；键在 key() 时已复制到 arena，这里只保存指针 + 长度。
     */
    struct JsonBuilder::Frame
    {
        void*         aggregate{nullptr};                           // ArrayRef 或 ObjectRef（按 is_object 解释）
        const char*   pending_key{nullptr};                         // 已声明但还没有值的键
        std::uint32_t pending_key_length{0};                        // 键字节数
        bool          has_pending_key{false};                       // 是否已有待插入的键
        bool          is_object{false};                             // true=对象帧，false=数组帧
    };

    JsonBuilder::JsonBuilder(const std::size_t block_size, const Common::SegmentAllocFn alloc_fn,
                             const Common::SegmentFreeFn free_fn) noexcept
        : m_document(block_size, alloc_fn, free_fn)
    {
        if (!m_document.is_ready())
        {
            m_error = JsonBuildError::not_initialized;
            return;
        }
        m_frames = m_document.arena().allocate_array<Frame>(kMaxJsonBuildDepth);
        if (m_frames == nullptr)
        {
            m_error = JsonBuildError::out_of_memory;
        }
    }

    void JsonBuilder::reset() noexcept
    {
        m_document.reset();
        m_root        = JsonValue{};
        m_has_root    = false;
        m_frame_count = 0;
        m_frames      = nullptr;
        m_error       = JsonBuildError::none;

        if (!m_document.is_ready())
        {
            m_error = JsonBuildError::not_initialized;
            return;
        }
        m_frames = m_document.arena().allocate_array<Frame>(kMaxJsonBuildDepth);
        if (m_frames == nullptr)
        {
            m_error = JsonBuildError::out_of_memory;
        }
    }

    JsonBuilder::Frame* JsonBuilder::frame_stack() noexcept
    {
        return static_cast<Frame*>(m_frames);
    }

    const JsonBuilder::Frame* JsonBuilder::frame_stack() const noexcept
    {
        return static_cast<const Frame*>(m_frames);
    }

    JsonBuildError JsonBuilder::fail(const JsonBuildError code) noexcept
    {
        if (m_error == JsonBuildError::none)
        {
            m_error = code;
        }
        // 与解析失败置空同口径：失败后不再对外暴露半个构建结果
        m_root        = JsonValue{};
        m_has_root    = false;
        m_frame_count = 0;
        return m_error;
    }

    JsonBuildError JsonBuilder::check_value_slot() const noexcept
    {
        if (m_frame_count == 0)
        {
            return m_has_root ? JsonBuildError::root_already_set : JsonBuildError::none;
        }

        const Frame& frame = frame_stack()[m_frame_count - 1];
        if (frame.is_object && !frame.has_pending_key)
        {
            return JsonBuildError::no_pending_key;
        }
        return JsonBuildError::none;
    }

    JsonBuildError JsonBuilder::commit_value(const JsonValue& value) noexcept
    {
        if (m_frame_count == 0)
        {
            m_root     = value;
            m_has_root = true;
            return JsonBuildError::none;
        }

        Frame& frame = frame_stack()[m_frame_count - 1];
        if (frame.is_object)
        {
            auto& ref = *static_cast<ObjectRef*>(frame.aggregate);
            if (!Detail::append_entry(m_document.arena(), ref, frame.pending_key, frame.pending_key_length,
                                      value)) [[unlikely]]
            {
                return fail(JsonBuildError::out_of_memory);
            }
            frame.pending_key        = nullptr;
            frame.pending_key_length = 0;
            frame.has_pending_key    = false;
            return JsonBuildError::none;
        }

        auto& ref = *static_cast<ArrayRef*>(frame.aggregate);
        if (!Detail::append_item(m_document.arena(), ref, value)) [[unlikely]]
        {
            return fail(JsonBuildError::out_of_memory);
        }
        return JsonBuildError::none;
    }

    JsonBuildError JsonBuilder::open_container(const bool is_object) noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }
        if (m_frame_count >= kMaxJsonBuildDepth) [[unlikely]]
        {
            return fail(JsonBuildError::depth_exceeded);
        }

        const JsonBuildError slot = check_value_slot();
        if (slot != JsonBuildError::none) [[unlikely]]
        {
            return fail(slot);
        }

        Common::SegmentedArena& arena = m_document.arena();
        JsonValue               value{};
        void*                   aggregate = nullptr;

        if (is_object)
        {
            auto* ref = arena.allocate_array<ObjectRef>(1);
            if (ref == nullptr) [[unlikely]]
            {
                return fail(JsonBuildError::out_of_memory);
            }
            *ref                = ObjectRef{};
            value.m_type        = JsonType::Object;
            value.m_payload.ptr = ref;
            aggregate           = ref;
        }
        else
        {
            auto* ref = arena.allocate_array<ArrayRef>(1);
            if (ref == nullptr) [[unlikely]]
            {
                return fail(JsonBuildError::out_of_memory);
            }
            *ref                = ArrayRef{};
            value.m_type        = JsonType::Array;
            value.m_payload.ptr = ref;
            aggregate           = ref;
        }

        const JsonBuildError committed = commit_value(value);
        if (committed != JsonBuildError::none) [[unlikely]]
        {
            return committed;
        }

        Frame& frame      = frame_stack()[m_frame_count];
        frame             = Frame{};
        frame.aggregate   = aggregate;
        frame.is_object   = is_object;
        ++m_frame_count;
        return JsonBuildError::none;
    }

    JsonBuildError JsonBuilder::open_object() noexcept
    {
        return open_container(true);
    }

    JsonBuildError JsonBuilder::open_array() noexcept
    {
        return open_container(false);
    }

    JsonBuildError JsonBuilder::close() noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }
        if (m_frame_count == 0) [[unlikely]]
        {
            return fail(JsonBuildError::no_open_container);
        }

        const Frame& frame = frame_stack()[m_frame_count - 1];
        if (frame.has_pending_key) [[unlikely]]
        {
            return fail(JsonBuildError::key_without_value);
        }
        --m_frame_count;
        return JsonBuildError::none;
    }

    JsonBuildError JsonBuilder::key(const std::string_view key_text) noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }
        if (m_frame_count == 0) [[unlikely]]
        {
            return fail(JsonBuildError::no_open_object);
        }

        Frame& frame = frame_stack()[m_frame_count - 1];
        if (!frame.is_object) [[unlikely]]
        {
            return fail(JsonBuildError::no_open_object);
        }
        if (frame.has_pending_key) [[unlikely]]
        {
            return fail(JsonBuildError::key_already_pending);
        }
        if (is_text_too_large(key_text.size())) [[unlikely]]
        {
            return fail(JsonBuildError::too_large);
        }

        if (!key_text.empty())
        {
            auto* copy = static_cast<char*>(m_document.arena().allocate(key_text.size(), 1));
            if (copy == nullptr) [[unlikely]]
            {
                return fail(JsonBuildError::out_of_memory);
            }
            std::memcpy(copy, key_text.data(), key_text.size());
            frame.pending_key        = copy;
            frame.pending_key_length = static_cast<std::uint32_t>(key_text.size());
        }
        frame.has_pending_key = true;
        return JsonBuildError::none;
    }

    JsonBuildError JsonBuilder::string(const std::string_view value) noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }

        const JsonBuildError slot = check_value_slot();
        if (slot != JsonBuildError::none) [[unlikely]]
        {
            return fail(slot);
        }
        if (is_text_too_large(value.size())) [[unlikely]]
        {
            return fail(JsonBuildError::too_large);
        }

        JsonValue out{};
        if (value.size() <= kInlineStringCapacity)
        {
            // 短串内联：连 arena 上的描述块都不需要（与解析侧完全一致）
            out.m_type = JsonType::InlineString;
            if (!value.empty())
            {
                std::memcpy(out.m_payload.chars, value.data(), value.size());
            }
            out.m_payload.chars[kInlineStringCapacity] = static_cast<char>(value.size());
            return commit_value(out);
        }

        Common::SegmentedArena& arena = m_document.arena();
        auto*                   ref   = arena.allocate_array<StringRef>(1);
        if (ref == nullptr) [[unlikely]]
        {
            return fail(JsonBuildError::out_of_memory);
        }
        auto* chars = static_cast<char*>(arena.allocate(value.size(), 1));
        if (chars == nullptr) [[unlikely]]
        {
            return fail(JsonBuildError::out_of_memory);
        }
        std::memcpy(chars, value.data(), value.size());
        ref->data           = chars;
        ref->length         = static_cast<std::uint32_t>(value.size());
        out.m_type          = JsonType::String;
        out.m_payload.ptr   = ref;
        return commit_value(out);
    }

    JsonBuildError JsonBuilder::integer(const std::int64_t value) noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }

        const JsonBuildError slot = check_value_slot();
        if (slot != JsonBuildError::none) [[unlikely]]
        {
            return fail(slot);
        }

        JsonValue out{};
        out.m_type        = JsonType::Int;
        out.m_payload.u64 = static_cast<std::uint64_t>(value);
        return commit_value(out);
    }

    JsonBuildError JsonBuilder::number(const double value) noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }

        const JsonBuildError slot = check_value_slot();
        if (slot != JsonBuildError::none) [[unlikely]]
        {
            return fail(slot);
        }

        JsonValue out{};
        out.m_type      = JsonType::Double;
        out.m_payload.d = value;
        return commit_value(out);
    }

    JsonBuildError JsonBuilder::boolean(const bool value) noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }

        const JsonBuildError slot = check_value_slot();
        if (slot != JsonBuildError::none) [[unlikely]]
        {
            return fail(slot);
        }

        JsonValue out{};
        out.m_type        = JsonType::Bool;
        out.m_payload.u64 = value ? 1u : 0u;
        return commit_value(out);
    }

    JsonBuildError JsonBuilder::null_value() noexcept
    {
        if (m_error != JsonBuildError::none) [[unlikely]]
        {
            return m_error;
        }

        const JsonBuildError slot = check_value_slot();
        if (slot != JsonBuildError::none) [[unlikely]]
        {
            return fail(slot);
        }
        return commit_value(JsonValue{});
    }

    std::size_t JsonBuilder::serialize(char* const out, const std::size_t capacity,
                                       const JsonWriteOptions& options, JsonWriteError* const err) const noexcept
    {
        return ID::Json::serialize(m_root, out, capacity, options, err);
    }

    std::size_t JsonBuilder::serialize(std::string& out, const JsonWriteOptions& options) const
    {
        JsonWriteError error = JsonWriteError::none;

        // 第一遍只探测所需字节数（out == nullptr），容量按结果精确分配，不做倍增重试
        const std::size_t needed = ID::Json::serialize(m_root, nullptr, 0, options, &error);
        if (error != JsonWriteError::none || needed == 0)
        {
            return 0;
        }

        out.resize(needed);
        const std::size_t written = ID::Json::serialize(m_root, out.data(), out.size(), options, &error);
        if (error != JsonWriteError::none)
        {
            return 0;
        }
        return written;
    }
} // namespace ID::Json
