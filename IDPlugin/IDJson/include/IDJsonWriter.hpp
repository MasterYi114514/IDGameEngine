#pragma once

#include <cstddef>
#include <cstdint>

#include "IDJsonCore.hpp"
#include "IDJsonValue.hpp"

/**
 *  JSON 序列化（把 JsonValue 写成文本）。
 *
 *  设计取舍：
 *  - 直写调用方的输出缓冲，不分配内存、不经过 arena、不产生字符串中间对象：容量不足时只统计
 *    所需字节数，不重新计算、不二次遍历。
 *  - 数字用 std::to_chars：整数取最短十进制；double 取最短往返表示，并在结果不含 '.'/'e' 时补
 *    ".0" 保证"读回来仍是 double"（类型保真，否则 round-trip 会变成 Int）。
 *  - 非有限 double（NaN/Inf）不是合法 JSON：报错并不产生输出，不做静默替换（不写 null、不写 0）。
 *  - 输出是紧凑或缩进格式，二者共用同一条写入路径；缩进不改变语义。
 *
 *  失败语义：
 *  - 失败以 JsonWriteError 返回，同时 serialize 返回 0；不抛异常。
 *  - 容量不足不算失败：返回"所需总字节数"，此时 out 的前 capacity 字节可能被部分写入，
 *    内容未定义，调用方不得使用。
 *
 *  线程契约：纯函数，只读输入与自己的栈上状态，可由多线程并发调用（读同一个 root 也安全）。
 */

namespace ID::Json
{
    /**
     *  序列化选项。
     */
    struct JsonWriteOptions
    {
        std::uint32_t indent = 0;   // 0 = 紧凑单行；> 0 = 每层缩进的空格数
    };

    /**
     *  @brief 序列化失败原因
     *  @note 取值顺序已冻结于本头；新增取值属于接口变更，必须升接口版本
     */
    enum class JsonWriteError : std::uint8_t
    {
        none = 0,                   // 成功
        non_finite_number,          // 值里有 NaN 或 ±Inf：不是合法 JSON，未产生输出
        too_deep                    // 嵌套层数超过 kMaxJsonWriteDepth
    };

    // 序列化的递归深度上限，与解析上限同值：解析出来的值不可能更深，手工构造的值也在这里被拦住，
    // 避免深递归打穿调用栈。消费者不能修改这个值。
    inline constexpr std::uint32_t kMaxJsonWriteDepth = 256;

    /**
     *  @brief 把值序列化成 JSON 文本
     *  @param root 待序列化的值；必须来自某个仍然存活的 JsonDocument
     *  @param out 输出缓冲；可为 nullptr（此时只探测所需字节数）
     *  @param capacity out 的可用字节数（不含结尾 NUL 的预留）
     *  @param options 缩进选项，见 JsonWriteOptions
     *  @param err 失败原因，可为 nullptr
     *  @return 成功时返回写入的字节数（不含结尾 NUL）；失败时返回 0
     *  @note 返回值为"所需总字节数"：capacity 不足时也返回该值，调用方据此扩容后重试；
     *        此时 out 的内容未定义，不得使用
     *  @note capacity 充足（capacity >= 返回值 + 1）时在 out[返回值] 处额外写入 '\0'，
     *        因此返回的字节数可以直接当作 C 字符串长度；capacity 恰好等于返回值时不写 NUL
     *  @note 不分配内存；不读 arena，只顺着值里的指针走
     */
    ID_JSON_API std::size_t serialize(const JsonValue& root, char* out, std::size_t capacity,
                                      const JsonWriteOptions& options,
                                      JsonWriteError* err = nullptr) noexcept;

    /**
     *  @brief 取序列化错误码的稳定文本
     *  @param error 错误码
     *  @return 形如 "non_finite_number" 的静态字符串；未知取值返回 "unknown_error"
     */
    ID_JSON_API const char* to_string(JsonWriteError error) noexcept;
} // namespace ID::Json
