#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "IDJsonCore.hpp"
#include "IDJsonDocument.hpp"

/**
 *  JSON 解析入口（严格 RFC 8259）。
 *
 *  设计取舍：
 *  - 递归下降、单字节推进：不建中间语法树，直接产出 JsonValue 与 arena 上的聚合描述块。
 *    空白判断走 256 项查表，字符串用 memchr 定界，只有出现转义时才回退到逐字节解码。
 *  - 两种所有权模式：`parse` 深拷贝全部字符串（输入缓冲可以立刻释放）；`parse_non_owning`
 *    对"不含转义且长度 > 7"的字符串直接引用输入缓冲（零拷贝），代价是输入缓冲必须活到文档
 *    reset()/析构。含转义的字符串在两种模式下都会被复制到 arena（无法零拷贝）。
 *  - 失败一律用返回值表达，不抛异常；错误位置的行/列只在失败冷路径上从缓冲区起点统计（memchr），
 *    因此成功路径不承担任何行/列统计成本。
 *  - 解析失败时文档被置空（根值为 null，arena 游标回退），不会留下"半个对象"。
 *
 *  线程契约：
 *  - 解析在调用线程内完成，不创建线程、不加锁；同一个 JsonDocument 不得被并发解析。
 *  - 错误日志桥的 attach/detach 可在任意线程调用（内部是原子指针），但**调用方必须保证**
 *    `logger` 指针在 detach 之前有效（见 attach_error_logger 的 @note）。
 */

// 只前向声明：IDLog / spdlog / fmt 的头不进 IDJson 的公开 include 链
namespace ID::Log
{
    class Logger;
} // namespace ID::Log

namespace ID::Json
{
    /**
     *  @brief 解析与初始化的错误码
     *  @note 取值顺序已冻结于本头；新增取值属于接口变更，必须升接口版本
     */
    enum class JsonError : std::uint8_t
    {
        none = 0,                   // 成功
        out_of_memory,              // arena 分配失败（段来源耗尽或请求尺寸溢出）
        unexpected_token,           // 语法意外的字符（含未闭合的字符串/数组/对象、缺失冒号或逗号）
        invalid_number,             // 数字字面量非法，或整数溢出后退化为 double 仍不可表示
        invalid_string_escape,      // 字符串里的 \ 转义序列非法（含 \u 后的十六进制位不足）
        invalid_utf16_surrogate,    // \uXXXX 代理对不完整或孤立（高代理后必须紧跟合法低代理）
        control_char_in_string,     // 字符串里出现未转义的 < 0x20 控制字符
        depth_exceeded,             // 嵌套层数超过 kMaxJsonDepth
        trailing_content,           // 根值之后还有非空白内容
        input_too_large,            // 输入长度超过 4 GiB - 1 字节（内部按 32 位计量长度，会回绕，故直接拒绝）
        not_initialized             // 文档的 arena 未配置成功（见 JsonDocument::is_ready）
    };

    /**
     *  解析失败的位置与原因。
     *  @note line/column 从 1 开始；offset 是从输入起始算的字节偏移
     *  @note 只有 offset 小于输入长度时，column/line 才可能指向具体字符；输入提前结束时
     *        offset 等于输入长度，表示"在末尾期待更多内容"
     */
    struct JsonParseError
    {
        JsonError       code{JsonError::none};      // 错误码
        std::uint32_t   offset{0};                  // 出错位置的字节偏移
        std::uint32_t   line{1};                    // 出错位置的行号（1 起）
        std::uint32_t   column{1};                  // 出错位置的列号（1 起，按字节计）
    };

    // 嵌套层数上限：根值为第 0 层，最深的容器允许到第 kMaxJsonDepth - 1 层；
    // 目的是把递归深度钉住，避免恶意输入打穿调用栈。消费者不能修改这个值。
    inline constexpr std::uint32_t kMaxJsonDepth = 256;

    /**
     *  @brief 解析 JSON 文本（owning：字符串全部深拷贝进文档的 arena）
     *  @param document 目标文档；解析前会被 reset()，成功时持有根值，失败时被置空
     *  @param text 输入文本；解析返回后可以立即释放（本函数不保留指向它的指针）
     *  @param out_error 失败时的错误位置与原因，可为 nullptr
     *  @return 成功返回 JsonError::none；否则返回具体错误码，document.root() 为 null
     *  @note 严格 RFC 8259：拒绝尾逗号、注释、单引号、NaN/Infinity 字面量与根值后的多余内容
     *  @note 单次输入上限为 4 GiB - 1 字节（内部长度计量是 32 位）：更大的输入直接返回
     *        JsonError::input_too_large，不进入解析
     *  @note 不是线程安全的：同一个 document 不得被并发解析
     */
    ID_JSON_API JsonError parse(JsonDocument& document, std::string_view text,
                                JsonParseError* out_error = nullptr) noexcept;

    /**
     *  @brief 解析 JSON 文本（non_owning：无转义且长度 > 7 字节的字符串直接引用输入缓冲）
     *  @param document 目标文档；语义同 parse
     *  @param text 输入文本；**必须存活到 document.reset() 或析构**，否则文档里的字符串视图悬垂
     *  @param out_error 失败时的错误位置与原因，可为 nullptr
     *  @return 成功返回 JsonError::none；否则返回具体错误码
     *  @note 零拷贝只覆盖"无转义、长度 > 7"的字符串与对象键：≤ 7 字节的字符串按内联存储（复制到值里），
     *        含转义的字符串仍会在 arena 上生成解码结果
     *  @note 输入缓冲的所有权不转移：本函数不释放、不修改输入
     */
    ID_JSON_API JsonError parse_non_owning(JsonDocument& document, std::string_view text,
                                           JsonParseError* out_error = nullptr) noexcept;

    /**
     *  @brief 取错误码的稳定文本（用于日志与诊断输出）
     *  @param error 错误码
     *  @return 形如 "unexpected_token" 的静态字符串；未知取值返回 "unknown_error"
     *  @note 返回的是编译期字符串字面量，不需要释放，可直接跨线程使用
     */
    ID_JSON_API const char* to_string(JsonError error) noexcept;

    /**
     *  @brief 挂接错误日志器：之后每次解析失败都会以 error 级别输出一条诊断
     *  @param logger 日志器指针，传 nullptr 等价于 detach_error_logger()；通常来自
     *                IDLog 的 create_logger/get_logger
     *  @param source_name 源名（例如模块或子系统名），作为消息前缀；**可为 nullptr**
     *  @note 只保存指针、不复制字符串：source_name 指向的内容必须在 detach 之前保持有效
     *        （通常传字符串字面量）
     *  @note `logger` 的存活与线程安全按 IDLog 的契约由调用方保证：本插件只在解析失败的冷路径
     *        读取该指针并调用一次 error()，不承担 logger 的创建、销毁与生命周期
     *  @note `logger` 必须来自 IDLog 以非 const 对象创建/取得的指针（库内经一次合法的 const_cast 调用
     *        其非 const 日志方法）；传入指向真正 const 对象的指针属于未定义行为
     *  @note 可在任意线程调用；内部用 relaxed 原子发布指针，与解析失败路径之间不提供更强的同步
     *  @note ID_JSON_ENABLE_LOG 为 0 时本函数编译为空操作（签名不变）
     */
    ID_JSON_API void attach_error_logger(const ID::Log::Logger* logger, const char* source_name) noexcept;

    /**
     *  @brief 断开错误日志器：之后不再产生任何解析失败日志
     *  @note 可在任意线程调用；调用方仍需按 IDLog 契约自行销毁 logger
     *  @note ID_JSON_ENABLE_LOG 为 0 时本函数编译为空操作
     */
    ID_JSON_API void detach_error_logger() noexcept;
} // namespace ID::Json
