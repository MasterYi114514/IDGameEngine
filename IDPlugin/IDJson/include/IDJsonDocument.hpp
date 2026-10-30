#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Arena/SegmentedArena.hpp"

#include "IDJsonCore.hpp"
#include "IDJsonValue.hpp"

/**
 *  JSON 文档：一个 arena 加一个根值。
 *
 *  设计取舍：
 *  - 文档内嵌 `ID::Common::SegmentedArena`，解析产生的全部长字符串、数组、对象都从它分配；
 *    文档析构或 reset() 时统一由它归还，"一个文档一次清理"，不存在单值释放接口。
 *  - 文档不可拷贝也不可移动（与内嵌的 SegmentedArena 一致）：任何拷贝都会让值里的指针指向
 *    被销毁的 arena。需要复制语义时用"序列化 + 重新解析"，或由调用方自行深拷贝。
 *  - 段来源默认是 malloc/free 包装，可替换为调用方自己的申请/归还函数；申请的段与归还的段必须
 *    同源（见 SegmentedArena 的契约）。
 *
 *  生命周期：
 *  - 文档内的值与字符串在 reset()/析构之前保持有效；这两个操作会一次性作废全部指针，且不调用
 *    任何析构函数（只允许 trivially destructible 的数据）。
 *  - reset() 复用已申请的常规段（游标回退），并归还解析期申请的巨段；析构归还全部段。
 *
 *  线程契约：
 *  - 一个文档在同一时刻只归一个线程使用；不同线程各自持有各自的文档并行使用是安全的。
 *  - 文档内部没有任何锁或原子操作，跨线程共享同一文档属于违约用法。
 *
 *  失败语义：
 *  - 构造不抛异常；参数非法（block_size 过小、alloc_fn 为空）或段来源不可用时 is_ready() 为 false，
 *    之后的 parse 返回 JsonError::not_initialized。
 *  - 解析失败时文档被置空（root() 为 null），失败原因经 JsonParseError 返回，不抛异常。
 */

namespace ID::Json
{
    // 解析错误的类型与入口定义在 IDJsonParser.hpp；此处只为友元声明做前置声明
    enum class JsonError : std::uint8_t;
    struct JsonParseError;

    // 默认段大小：64 KiB。段大小影响"每次向来源申请多少"与碎片分布，消费者可按自己的体量覆盖
    inline constexpr std::size_t kDefaultBlockSize = 64 * 1024;

    /**
     *  @brief 默认段来源：malloc 包装
     *  @param size 申请字节数
     *  @param alignment 对齐要求，必须不超过 alignof(std::max_align_t)（malloc 的保证）
     *  @return 申请到的内存；对齐要求超过 max_align 或分配失败时返回 nullptr
     *  @note 与 default_segment_free 同源，二者必须成对使用
     */
    ID_JSON_API void* default_segment_alloc(std::size_t size, std::size_t alignment) noexcept;

    /**
     *  @brief 默认段归还：free 包装
     *  @param block 由 default_segment_alloc 返回的原始指针（可为 nullptr）
     */
    ID_JSON_API void default_segment_free(void* block) noexcept;

    class ID_JSON_API JsonDocument
    {
    public:
        /**
         *  @brief 构造文档并配置内嵌 arena
         *  @param block_size 每个常规段的总字节数（含段头），必须大于段头尺寸；默认 64 KiB
         *  @param alloc_fn 段来源申请函数，不能为空；返回值必须至少 alignof(std::max_align_t) 对齐
         *  @param free_fn 段归还函数；为空时文档只丢弃段链、不归还来源内存（回收由调用方负责）
         *  @note 不抛异常：参数非法或配置失败时 is_ready() 为 false，解析返回 not_initialized
         */
        explicit JsonDocument(std::size_t block_size = kDefaultBlockSize,
                              Common::SegmentAllocFn alloc_fn = default_segment_alloc,
                              Common::SegmentFreeFn free_fn = default_segment_free) noexcept;

        // 析构归还全部段（与构造侧的段来源同源）；值里的指针随之一并失效
        ~JsonDocument() noexcept { m_arena.shutdown(); }

        JsonDocument(const JsonDocument&)            = delete;
        JsonDocument& operator=(const JsonDocument&) = delete;
        JsonDocument(JsonDocument&&)                 = delete;
        JsonDocument& operator=(JsonDocument&&)      = delete;

        /**
         *  @brief arena 是否配置成功
         *  @return 配置成功返回 true；false 时 parse 一律返回 JsonError::not_initialized
         */
        ID_FORCEINLINE bool is_ready() const noexcept { return m_ready; }

        /**
         *  @brief 取根值
         *  @return 根值引用；未解析或解析失败时为 null 值（is_null() 为 true）
         *  @note 引用在 reset()/析构前有效
         */
        ID_FORCEINLINE const JsonValue& root() const noexcept { return m_root; }

        /**
         *  @brief 置空文档：回退 arena 游标、归还巨段，并把根值置为 null
         *  @note 一次性作废文档内全部指针与 string_view；不调用任何析构函数
         *  @note 常规段保留复用，不向来源归还；未初始化时是无操作
         */
        void reset() noexcept;

        /**
         *  @brief 访问内嵌 arena（解析器内部使用，也可用于观察容量统计）
         *  @return arena 引用；文档析构前有效
         *  @note 消费者从 arena 手工分配的内存不受文档管理，且会被 reset() 一并作废；
         *        正常使用只需要 parse/serialize 与 root()
         */
        ID_FORCEINLINE Common::SegmentedArena& arena() noexcept { return m_arena; }
        ID_FORCEINLINE const Common::SegmentedArena& arena() const noexcept { return m_arena; }

        // 解析入口（定义在 IDJsonParser.hpp）需要写入根值：根值本身已经落在本文档的 arena 上，
        // 因此只在这里授权访问，不额外开放"任意设置根值"的公开接口
        friend JsonError parse(JsonDocument& document, std::string_view text, JsonParseError* out_error) noexcept;
        friend JsonError parse_non_owning(JsonDocument& document, std::string_view text,
                                          JsonParseError* out_error) noexcept;

    private:
        Common::SegmentedArena  m_arena;            // 全部长串/数组/对象的存储来源
        JsonValue               m_root{};           // 根值；未解析或失败时为全零的 null 值
        bool                    m_ready{false};     // arena 是否配置成功
    };
} // namespace ID::Json
