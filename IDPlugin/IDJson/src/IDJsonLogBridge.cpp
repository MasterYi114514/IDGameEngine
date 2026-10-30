#include "IDJsonParser.hpp"

#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

#include "IDJsonCore.hpp"
#include "IDJsonDetail.hpp"

#if ID_JSON_ENABLE_LOG
    // 本插件唯一包含 IDLog 头的翻译单元：IDLog / spdlog / fmt 的类型不出现在任何公开头里
    #include "IDLog.hpp"
#endif

/**
 *  解析失败的错误日志桥。
 *
 *  实现说明：
 *  - 条件编译只在文件顶层出现：`#if ID_JSON_ENABLE_LOG` 在文件作用域把"启用实现"与"空操作实现"
 *    整体分成两支，任何花括号 `{}` 内部都没有 `#if/#ifdef/#else/#endif`。对外的 `ID::Json` /
 *    `ID::Json::Detail` 命名空间、函数签名与语义逐字一致；修改任一分支时必须同步另一分支。
 *    消费者代码不需要条件编译。
 *  - 开关 ID_JSON_ENABLE_LOG 为 1 时才包含 IDLog 头并链接 IDLog；为 0 时 attach/detach 与
 *    输出路径都是空操作，签名不变。
 *  - 只保存指针，不复制字符串；发布顺序是"先源名、后日志器"，解析失败路径读到非空日志器时
 *    源名已经就位。两个指针都是 relaxed 原子：这里不需要比"指针可见性"更强的同步。
 *  - 消息用分步拼接（to_chars + 逐字符追加）而不是一次 snprintf 塞多个参数：本机 GCC 16.2 + UCRT
 *    在单次 snprintf 传 11 个可变参数时会在格式化路径内崩溃，分步拼接同时避开了这条工具链缺陷。
 *  - 只在解析失败的冷路径产生一条 error 级日志；解析成功路径不读原子、不碰日志。
 */

#if ID_JSON_ENABLE_LOG
namespace
{
    std::atomic<const ID::Log::Logger*> g_error_logger{nullptr};    // 已挂接的错误日志器
    std::atomic<const char*>            g_error_source{nullptr};    // 源名（只存指针，不复制）

    // 定长缓冲上的分步拼接：容量不足时只截断，不分配、不越界
    class TextBuilder
    {
    public:
        TextBuilder(char* const buffer, const std::size_t capacity) noexcept
            : m_buffer(buffer)
            , m_capacity(capacity)
        {
        }

        void append_literal(const char* const text) noexcept
        {
            for (const char* scan = text; *scan != '\0'; ++scan)
            {
                append_char(*scan);
            }
        }

        void append(const std::string_view text) noexcept
        {
            for (const char raw : text)
            {
                append_char(raw);
            }
        }

        void append_u32(const std::uint32_t value) noexcept
        {
            char       digits[10];
            const auto result = std::to_chars(digits, digits + sizeof(digits), value);
            if (result.ec != std::errc{})
            {
                return;
            }

            for (const char* scan = digits; scan != result.ptr; ++scan)
            {
                append_char(*scan);
            }
        }

        // 只返回真正落在缓冲里的部分：超长源名被截断而不是越界读
        std::string_view view() const noexcept
        {
            return std::string_view(m_buffer, m_length < m_capacity ? m_length : m_capacity);
        }

    private:
        void append_char(const char ch) noexcept
        {
            if (m_length < m_capacity)
            {
                m_buffer[m_length] = ch;
            }
            ++m_length;
        }

    private:
        char*       m_buffer{nullptr};      // 输出缓冲（栈上，生命周期限于本次调用）
        std::size_t m_capacity{0};          // 缓冲容量
        std::size_t m_length{0};            // 已请求写入的字符数（可能超过容量）
    };
} // namespace

namespace ID::Json::Detail
{
    ID_NOINLINE ID_COLD void log_parse_error(const JsonParseError& error) noexcept
    {
        const ID::Log::Logger* const logger = g_error_logger.load(std::memory_order_relaxed);
        if (logger == nullptr)
        {
            return;     // 未挂接：零日志，也不做任何格式化
        }

        const char* const source = g_error_source.load(std::memory_order_relaxed);

        char        buffer[192];
        TextBuilder text(buffer, sizeof(buffer));
        text.append_literal("[IDJson] ");
        if (source != nullptr)
        {
            text.append(std::string_view(source));
            text.append_literal(": ");
        }
        text.append_literal("parse failed, code=");
        text.append_literal(ID::Json::to_string(error.code));
        text.append_literal(" offset=");
        text.append_u32(error.offset);
        text.append_literal(" line=");
        text.append_u32(error.line);
        text.append_literal(" column=");
        text.append_u32(error.column);

        // IDLog 的日志方法是非 const 成员（内部会读写后端状态），而 logger 指针按 IDLog 契约来自
        // 消费者以非 const 对象 create_logger/get_logger 得到的指针：这里的 const_cast 不会命中
        // 真正的 const 对象，不构成未定义行为
        const_cast<ID::Log::Logger*>(logger)->error("{}", text.view());
    }
} // namespace ID::Json::Detail

namespace ID::Json
{
    void attach_error_logger(const ID::Log::Logger* const logger, const char* const source_name) noexcept
    {
        // 先源名、后日志器：解析失败路径读到非空日志器时源名已经就位
        g_error_source.store(source_name, std::memory_order_relaxed);
        g_error_logger.store(logger, std::memory_order_relaxed);
    }

    void detach_error_logger() noexcept
    {
        g_error_logger.store(nullptr, std::memory_order_relaxed);
        g_error_source.store(nullptr, std::memory_order_relaxed);
    }
} // namespace ID::Json
#else
// OFF 分支没有日志器指针、没有格式化辅助函数，因此不需要匿名 namespace 的内部实现
namespace ID::Json::Detail
{
    ID_NOINLINE ID_COLD void log_parse_error(const JsonParseError& error) noexcept
    {
        (void)error;
    }
} // namespace ID::Json::Detail

namespace ID::Json
{
    void attach_error_logger(const ID::Log::Logger* const logger, const char* const source_name) noexcept
    {
        (void)logger;
        (void)source_name;
    }

    void detach_error_logger() noexcept
    {
    }
} // namespace ID::Json
#endif
