#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "IDJson.hpp"

/**
 *  IDJson 测试域的私有共享设施（只被 `src/idjson` 下的实现文件包含，不在公开 include 路径）。
 *
 *  内容分三组：
 *  1. 固定测试文档：配置类中小文档、大对象数组混合文档、深嵌套 + 长字符串文档。
 *     全部按固定参数手工拼装，没有随机数与时间依赖，同一份代码在任何机器上产出逐字节相同的文本。
 *  2. 段来源计数分配器：`Common::SegmentAllocFn` 是函数指针、无法捕获状态，因此计数落在进程级变量上；
 *     同时提供"达到 N 次后一律失败"的上限，用于受控注入 `JsonError::out_of_memory`
 *     （公开契约允许调用方替换段来源，这不是修改被测实现）。
 *  3. DOM 指纹：独立于被测实现的遍历器，把 DOM 规范化成可逐字节比较的文本。
 *     用途是断言"owning 与 non_owning 两种解析模式产出的结构完全一致"，
 *     以及"序列化 → 重新解析"的 round-trip 不改变结构；它不调用被测插件的序列化器，
 *     因此不是"用被测实现证明被测实现"。
 *
 *  纪律：
 *  - 本文件不写报告、不调用 `Check`：判定一律留在用例文件里，便于区分"数据准备"与"契约断言"。
 *  - 全局状态只有计数分配器（`constinit`，无运行期构造函数）与测试文档（调用方持有，不入全局）。
 */

namespace ID::Bench::JsonFixture
{
    using std::size_t;

    // ==================== 固定测试文档 ====================

    struct SampleDocument
    {
        std::string    text;        // JSON 文本（UTF-8）
        std::size_t    bytes;       // 文本字节数（= text.size()，单独存一份便于拼报告）
        const char*    label;       // 文档标识（报告与 detail 用）
    };

    /// @brief 无符号整数 + 固定两位补零（用于生成确定的键名/名称，避免随机）
    inline void AppendPadded(std::string& out, unsigned value, unsigned width)
    {
        char digits[16] = {};
        unsigned length = 0;
        do
        {
            digits[length] = static_cast<char>('0' + (value % 10u));
            ++length;
            value /= 10u;
        } while (value != 0);
        while (length < width)
        {
            digits[length] = '0';
            ++length;
        }
        while (length > 0)
        {
            --length;
            out.push_back(digits[length]);
        }
    }

    /**
     *  @brief 配置类中小文档（约 1.5 KB）
     *  @return 文本与字节数
     *  @note 结构取自"项目配置"形态：嵌套对象、字符串数组、布尔、整数、浮点与短键；
     *        刻意保持字符串多为 5–20 字节，命中内联与长串两条存储路径
     */
    inline SampleDocument MakeConfigDocument()
    {
        SampleDocument document;
        std::string& text = document.text;

        text += "{\n";
        text += "  \"project\": \"idgame-demo\",\n";
        text += "  \"version\": \"1.0.3\",\n";
        text += "  \"engine\": {\"name\": \"IDGameEngine\", \"arch\": \"x64\", \"lto\": false, \"jobs\": 8},\n";
        text += "  \"targets\": [\"client\", \"server\", \"tools\"],\n";
        text += "  \"plugins\": [\n";

        constexpr unsigned kPluginCount = 12;
        for (unsigned index = 0; index < kPluginCount; ++index)
        {
            if (index != 0)
            {
                text += ",\n";
            }
            text += "    {\"name\": \"plugin-";
            AppendPadded(text, index, 2);
            text += "\", \"kind\": \"static\", \"enabled\": true, \"level\": \"info\", \"block_size\": ";
            text += std::to_string(4096u + index * 1024u);
            text += "}";
        }

        text += "\n  ],\n";
        text += "  \"paths\": {\"source\": \"src/main.cpp\", \"output\": \"out/build/release\", \"cache\": \"out/cache\"},\n";
        text += "  \"limits\": {\"depth\": 64, \"bytes\": 1048576, \"ratio\": 0.75, \"min_scale\": -1.5e-3,"
                " \"max_scale\": 2.5e+4},\n";
        text += "  \"flags\": [1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233]\n";
        text += "}\n";

        document.bytes = text.size();
        document.label = "config";
        return document;
    }

    /**
     *  @brief 大对象数组混合文档（约 150 KB）
     *  @return 文本与字节数
     *  @note 800 个对象，每对象 6 个成员（id/name/active/ratio/tags/desc）；
     *        成员数 ≤ 8，因此对象成员走线性扫描路径，`tags` 覆盖短串数组
     */
    inline SampleDocument MakeBulkDocument()
    {
        SampleDocument document;
        std::string& text = document.text;

        constexpr const char* kRatios[] = {"0.5", "0.25", "1.75", "12.5"};
        constexpr const char* kDescriptions[] = {
            "config payload description for a stable benchmark document with fixed content 0000",
            "second description kind with the same length so the document size stays deterministic",
            "third description kind, again fixed length, used to keep every object byte-identical",
            "fourth description kind; the parser sees mixed short and long string values here",
        };

        text += "[";
        constexpr unsigned kObjectCount = 800;
        for (unsigned index = 0; index < kObjectCount; ++index)
        {
            if (index != 0)
            {
                text += ",\n";
            }

            text += "{\"id\": ";
            text += std::to_string(index);
            text += ", \"name\": \"item-";
            AppendPadded(text, index, 4);
            text += "\", \"active\": true, \"ratio\": ";
            text += kRatios[index % 4u];
            text += ", \"tags\": [\"alpha\", \"beta\", \"gamma\"], \"desc\": \"";
            text += kDescriptions[index % 4u];
            text += "\"}";
        }
        text += "]";

        document.bytes = text.size();
        document.label = "bulk";
        return document;
    }

    /**
     *  @brief 深嵌套 + 长字符串文档（约 25 KB）
     *  @return 文本与字节数
     *  @note 96 层嵌套对象（远深于典型配置，但低于 kMaxJsonDepth=256）：每层 4 个成员
     *        （name/pad/flags/child），最内层再加两个 8 KB 长字符串，覆盖"长串走 arena"的路径
     */
    inline SampleDocument MakeNestedDocument()
    {
        SampleDocument document;
        std::string& text = document.text;

        constexpr unsigned kDepth = 96;
        text += "{";
        for (unsigned level = 0; level < kDepth; ++level)
        {
            if (level != 0)
            {
                // 上一层的 "child" 值就是本层对象：根对象是第 0 层，其余每层多一个 "child" 容器
                text += "\"child\": {";
            }
            text += "\"name\": \"nested-level-";
            AppendPadded(text, level, 2);
            text += "\", \"pad\": \"padding-block-";
            AppendPadded(text, level, 2);
            text += "\", \"flags\": [true, false]";
            if (level + 1 != kDepth)
            {
                text += ", ";
            }
        }

        // 最内层（第 kDepth-1 层）额外两个长字符串
        text += ", \"long_a\": \"";
        text.append(8192, 'a');
        text += "\", \"long_b\": \"";
        text.append(8192, 'b');
        text += "\"";

        // 根对象 + 每个 "child" 容器各关闭一次：共 kDepth 个右花括号
        text.append(kDepth, '}');

        document.bytes = text.size();
        document.label = "nested";
        return document;
    }

    /**
     *  @brief 只含一个超长字符串值的文档（用于巨段路径与大输入）
     *  @param payload_bytes 字符串内容的字节数
     *  @return JSON 文本 `{"big":"<payload_bytes 个 'x'>"}`
     */
    inline std::string MakeLongStringDocument(size_t payload_bytes)
    {
        std::string text;
        text.reserve(payload_bytes + 32);
        text += "{\"big\": \"";
        text.append(payload_bytes, 'x');
        text += "\"}";
        return text;
    }

    /// @brief 指定层数的嵌套空数组文本 `[[[...]]]`（1 层 = `[[]]`）
    inline std::string MakeNestedArrayText(unsigned depth)
    {
        std::string text;
        text.reserve(depth * 2u);
        text.append(depth, '[');
        text.append(depth, ']');
        return text;
    }

    /// @brief 指定层数的嵌套对象文本 `{"a":{"a":null}}`（1 层 = `{"a":null}`）
    inline std::string MakeNestedObjectText(unsigned depth)
    {
        std::string text;
        text.reserve(depth * 5u + 8u);
        for (unsigned level = 0; level < depth; ++level)
        {
            text += "{\"a\":";
        }
        text += "null";
        text.append(depth, '}');
        return text;
    }

    /// @brief 指定元素个数的整数数组文本 `[0,1,2,...]`
    inline std::string MakeIntegerArrayDocument(size_t count)
    {
        std::string text;
        text.reserve(count * 7u + 2u);
        text += "[";
        for (size_t index = 0; index < count; ++index)
        {
            if (index != 0)
            {
                text += ",";
            }
            text += std::to_string(index);
        }
        text += "]";
        return text;
    }

    // ==================== 段来源计数分配器 ====================

    /**
     *  段来源计数：只统计成功申请的调用与字节数，以及归还次数。
     *  `alloc_limit == 0` 表示不限；非 0 时第 `alloc_limit + 1` 次申请起一律返回 nullptr，
     *  用于让 `parse` 在解析中途拿到 `JsonError::out_of_memory`。
     */
    struct SegmentCounters
    {
        size_t  alloc_calls;
        size_t  free_calls;
        size_t  alloc_bytes;
        size_t  alloc_limit;
    };

    // 全零常量初始化（alloc_limit == 0 即"不限"），没有运行期构造函数
    inline constinit SegmentCounters g_segment_counters{};

    /// @brief 重置计数；limit 为 0 表示不限制申请次数
    inline void ResetSegmentCounters(size_t limit = 0)
    {
        g_segment_counters.alloc_calls = 0;
        g_segment_counters.free_calls  = 0;
        g_segment_counters.alloc_bytes = 0;
        g_segment_counters.alloc_limit = limit;
    }

    /// @brief 计数版段申请：与 default_segment_alloc 同源（malloc），只多一次计数与上限判定
    /// @note 必须带 noexcept：SegmentAllocFn 是 noexcept 函数指针类型
    inline void* CountingSegmentAlloc(size_t size, size_t alignment) noexcept
    {
        // 段来源契约只允许 ≤ alignof(std::max_align_t) 的对齐，malloc 的保证已覆盖；
        // 这一判定与 IDJson::default_segment_alloc 一致，超出时返回 nullptr 而不是返回错位内存
        if (alignment > alignof(std::max_align_t))
        {
            return nullptr;
        }
        if (g_segment_counters.alloc_limit != 0 && g_segment_counters.alloc_calls >= g_segment_counters.alloc_limit)
        {
            return nullptr;
        }

        void* block = std::malloc(size);
        if (block == nullptr)
        {
            return nullptr;
        }

        ++g_segment_counters.alloc_calls;
        g_segment_counters.alloc_bytes += size;
        return block;
    }

    /// @brief 计数版段归还：与 CountingSegmentAlloc 成对使用（同样必须 noexcept）
    inline void CountingSegmentFree(void* block) noexcept
    {
        if (block != nullptr)
        {
            ++g_segment_counters.free_calls;
        }
        std::free(block);
    }

    // ==================== DOM 指纹（独立遍历器） ====================

    inline void AppendHexBits(std::string& out, std::uint64_t bits)
    {
        constexpr char kHex[] = "0123456789abcdef";
        out += "0x";
        for (int shift = 60; shift >= 0; shift -= 4)
        {
            out.push_back(kHex[(bits >> static_cast<unsigned>(shift)) & 0xFu]);
        }
    }

    /**
     *  @brief 把 DOM 规范化成可逐字节比较的文本
     *  @param value 待遍历的根值
     *  @param out 追加目标
     *  @note 浮点按位比较（`0x...`），因此 `-0.0` 与 `0.0` 会被区分；字符串带长度前缀，
     *        因此内嵌 NUL 不会被截断
     *  @note 只使用公开访问器与公开的成员遍历入口（`members_begin`/`members_end`）
     */
    inline void AppendFingerprint(const ID::Json::JsonValue& value, std::string& out)
    {
        using ID::Json::JsonType;

        switch (value.type())
        {
            case JsonType::Null:
                out += "n;";
                return;

            case JsonType::Bool:
                out += value.as_bool() ? "b1;" : "b0;";
                return;

            case JsonType::Int:
                out += "i";
                out += std::to_string(value.as_int64());
                out += ";";
                return;

            case JsonType::Double:
                out += "d";
                AppendHexBits(out, std::bit_cast<std::uint64_t>(value.as_double()));
                out += ";";
                return;

            case JsonType::InlineString:
            case JsonType::String:
            {
                const std::string_view text = value.as_string();
                out += "s";
                out += std::to_string(text.size());
                out += ":";
                out.append(text.data(), text.size());
                out += ";";
                return;
            }

            case JsonType::Array:
            {
                out += "a";
                out += std::to_string(value.size());
                out += "[";
                for (const ID::Json::JsonValue* item = value.begin(); item != value.end(); ++item)
                {
                    AppendFingerprint(*item, out);
                }
                out += "];";
                return;
            }

            case JsonType::Object:
            {
                out += "o";
                out += std::to_string(value.size());
                out += "{";
                for (const ID::Json::Detail::ObjectEntry* entry = value.members_begin();
                     entry != value.members_end(); ++entry)
                {
                    out += "k";
                    out += std::to_string(entry->key_length);
                    out += ":";
                    if (entry->key != nullptr && entry->key_length != 0)
                    {
                        out.append(entry->key, entry->key_length);
                    }
                    out += "=";
                    AppendFingerprint(entry->value, out);
                }
                out += "};";
                return;
            }
        }

        out += "?;";     // JsonType 的全部取值都已处理；保留标记以免将来新增取值时静默通过
    }

    /// @brief 指纹文本（失败/空输入时返回空串，调用方可据空串判定）
    inline std::string Fingerprint(const ID::Json::JsonValue& value)
    {
        std::string out;
        AppendFingerprint(value, out);
        return out;
    }

    // ==================== 其他小工具 ====================

    /// @brief 双精度值的位模式（round-trip 与 -0.0 判定用）
    inline std::uint64_t DoubleBits(double value)
    {
        return std::bit_cast<std::uint64_t>(value);
    }

    /**
     *  @brief `data[0, length)` 是否完全落在 `input` 的字节区间内
     *  @note 用整数地址比较，避免对无关指针做关系比较；`length == 0` 时只检查起点落在区间内
     */
    inline bool IsInsideInput(const std::string& input, const char* data, size_t length)
    {
        if (data == nullptr || input.empty())
        {
            return false;
        }

        const auto begin_address = reinterpret_cast<std::uintptr_t>(input.data());
        const auto end_address   = begin_address + input.size();
        const auto data_address  = reinterpret_cast<std::uintptr_t>(data);
        if (data_address < begin_address || data_address >= end_address)
        {
            return false;
        }
        const size_t offset = static_cast<size_t>(data_address - begin_address);
        return length <= input.size() - offset;
    }
} // namespace ID::Bench::JsonFixture
