#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Arena/SegmentedArena.hpp"

#include "IDJsonCore.hpp"
#include "IDJsonDocument.hpp"
#include "IDJsonValue.hpp"
#include "IDJsonWriter.hpp"

/**
 *  JSON 文档的构建入口（把值写进 arena，产出与 parse 同构的只读 DOM）。
 *
 *  设计取舍：
 *  - 构建与解析共用同一套存储布局与同一份聚合追加实现：字符串 ≤ 7 字节内联在值里，更长的字符串、
 *    数组与对象分配在文档的 arena 上。因此构建产物与 parse 的返回值在只读访问上完全同构
 *    （`find`、`operator[]`、`size`、遍历、`serialize` 都可以直接用在两者上）。
 *  - 序列化不另立实现：`JsonBuilder::serialize` 只是对 `IDJsonWriter.hpp` 的 `serialize` 的转发，
 *    转义、数字格式、缩进与深度上限与读侧完全一致，不产生第二套写出路径。
 *  - 只提供"构建"顺序写入，不提供原地修改：没有 set/erase 一类的接口，一个值一旦提交就只读。
 *    要改内容只能 `reset()` 后重建（文档与 parse 的只读契约一致）。
 *  - 构建期只在需要时为长字符串、聚合描述块与其扩容向 arena 申请内存：标量（null/bool/整数/浮点）
 *    与短字符串零分配。arena 不回收单块，失败或 reset() 时统一交给文档归还。
 *  - 失败用返回值表达，不抛异常；首次失败后进入粘性失败态（见 `JsonBuildError`），避免调用方
 *    在部分构建的树上继续追加。
 *
 *  生命周期：
 *  - 构建器内嵌自己的 `JsonDocument`；构建产物的全部指针与 string_view 在 `reset()` 或构建器析构
 *    之前有效。`root()` 返回的引用同样如此。
 *  - 构建器不可拷贝、不可移动（产物指针指向它自己的 arena）。
 *
 *  线程契约：一个构建器在同一时刻只归一个线程使用，内部无锁无原子；不同线程各自持有各自的构建器
 *  并行构建是安全的。构建完成后，`root()` 交给 `serialize` 只读使用可由多线程并发进行。
 */

namespace ID::Json
{
    /**
     *  @brief 构建失败的原因
     *  @note 取值顺序已冻结于本头；新增取值属于接口变更，必须升接口版本
     */
    enum class JsonBuildError : std::uint8_t
    {
        none = 0,                   // 成功
        not_initialized,            // 内嵌文档的 arena 或帧栈未配置成功（见 is_ready）
        out_of_memory,              // arena 分配失败（段来源耗尽或请求尺寸溢出）
        depth_exceeded,             // 嵌套层数达到 kMaxJsonBuildDepth
        root_already_set,           // 根值已写入，又追加了第二个根值
        no_open_object,             // 在当前没有打开对象时调用 key()
        no_open_container,          // 在当前没有打开的容器时调用 close()
        no_pending_key,             // 在对象里追加值，但之前没有调用 key()
        key_already_pending,        // 上一个键还没有对应的值，又调用了 key()
        key_without_value,          // 关闭对象时最后一个键还没有对应的值
        too_large                   // 字符串/键超过 4 GiB - 1 字节，元素数超过 32 位计数上限
    };

    // 构建嵌套层数上限：与写出上限同值，保证"构建得出来就一定序列化得出来"
    inline constexpr std::uint32_t kMaxJsonBuildDepth = kMaxJsonWriteDepth;

    class ID_JSON_API JsonBuilder
    {
    public:
        /**
         *  @brief 构造构建器（内嵌文档按参数配置 arena）
         *  @param block_size 每个常规段的总字节数（含段头），必须大于段头尺寸；默认 64 KiB
         *  @param alloc_fn 段来源申请函数，不能为空；返回值必须至少 alignof(std::max_align_t) 对齐
         *  @param free_fn 段归还函数；为空时构建器只丢弃段链、不归还来源内存（回收由调用方负责）
         *  @note 不抛异常：参数非法或配置失败时 is_ready() 为 false，之后的构建调用返回
         *        not_initialized；帧栈申请失败时返回 out_of_memory
         */
        explicit JsonBuilder(std::size_t block_size = kDefaultBlockSize,
                             Common::SegmentAllocFn alloc_fn = default_segment_alloc,
                             Common::SegmentFreeFn free_fn = default_segment_free) noexcept;

        ~JsonBuilder() noexcept = default;

        JsonBuilder(const JsonBuilder&)            = delete;
        JsonBuilder& operator=(const JsonBuilder&) = delete;
        JsonBuilder(JsonBuilder&&)                 = delete;
        JsonBuilder& operator=(JsonBuilder&&)      = delete;

        /**
         *  @brief 是否可以开始构建
         *  @return 内嵌文档与帧栈都就绪返回 true；false 时构建调用一律返回 not_initialized/out_of_memory
         *  @note 构建过程中出现的错误（如没有打开的容器就 close()）不改变本函数的返回值
         */
        ID_FORCEINLINE bool is_ready() const noexcept { return m_document.is_ready() && m_frames != nullptr; }

        /**
         *  @brief 取构建结果的根值
         *  @return 根值引用；尚未构建任何值或已失败时为 null 值（is_null() 为 true）
         *  @note 引用在 reset()/析构前有效；返回值可以直接传给 `IDJsonWriter.hpp` 的 `serialize`
         */
        ID_FORCEINLINE const JsonValue& root() const noexcept { return m_root; }

        /**
         *  @brief 丢弃已构建的内容并复用 arena：回到"什么都没构建"的状态
         *  @note 一次性作废本构建器发出的全部指针与 string_view；不调用任何析构函数
         *  @note 常规段保留复用；粘性失败态被清除，可以重新构建
         */
        void reset() noexcept;

        /**
         *  @brief 打开一个对象：它成为根值，或成为当前容器末尾的一个成员/元素
         *  @return 成功返回 none；否则返回失败原因（见 JsonBuildError）
         *  @note 打开后必须用 close() 关闭；对象里只能先 key() 再追加值
         */
        JsonBuildError open_object() noexcept;

        /**
         *  @brief 打开一个数组：它成为根值，或成为当前容器末尾的一个成员/元素
         *  @return 成功返回 none；否则返回失败原因
         *  @note 打开后必须用 close() 关闭
         */
        JsonBuildError open_array() noexcept;

        /**
         *  @brief 关闭当前打开的容器（对象或数组）
         *  @return 成功返回 none；没有打开的容器、或对象最后一个键还没有值、或已失败时返回对应错误
         */
        JsonBuildError close() noexcept;

        /**
         *  @brief 为对象里的下一个值指定键
         *  @param key 键字符；按字节保存并原样写出（区分大小写，不做 Unicode 规范化）
         *  @return 成功返回 none；当前没有打开对象、上一个键还没有值、键超长或已失败时返回对应错误
         *  @note 键字符被复制到 arena，调用返回后 key 指向的内容可以释放
         *  @note 同一个对象里重复键按"后写覆盖值、位置保持首次插入序"处理，与解析侧一致
         */
        JsonBuildError key(std::string_view key) noexcept;

        /**
         *  @brief 追加一个字符串值
         *  @param value 字符内容；按字节保存，不做 UTF-8 校验
         *  @return 成功返回 none；否则返回失败原因
         *  @note ≤ 7 字节内联在值里（不分配）；更长的内容复制到 arena，调用返回后可以释放
         */
        JsonBuildError string(std::string_view value) noexcept;

        /**
         *  @brief 追加一个 64 位有符号整数值（JsonType::Int）
         *  @param value 取值；序列化为最短十进制表示，不依赖 locale
         */
        JsonBuildError integer(std::int64_t value) noexcept;

        /**
         *  @brief 追加一个双精度浮点值（JsonType::Double）
         *  @param value 取值；非有限值（NaN/±Inf）可以写入，但序列化时按 JsonWriteError::non_finite_number 失败
         *  @note 序列化为最短往返表示，结果不含 '.'/'e' 时补 ".0"，读回来仍是 Double
         */
        JsonBuildError number(double value) noexcept;

        /**
         *  @brief 追加一个布尔值
         */
        JsonBuildError boolean(bool value) noexcept;

        /**
         *  @brief 追加一个 null 值
         */
        JsonBuildError null_value() noexcept;

        /**
         *  @brief 把构建结果序列化到调用方缓冲
         *  @param out 输出缓冲；可为 nullptr（此时只探测所需字节数）
         *  @param capacity out 的可用字节数（不含结尾 NUL 的预留）
         *  @param options 缩进选项，语义与 `IDJsonWriter.hpp` 一致
         *  @param err 失败原因，可为 nullptr
         *  @return 成功时返回写入的字节数（不含结尾 NUL）；失败时返回 0
         *  @note 与 `IDJsonWriter.hpp` 的 `serialize` 是同一条路径：本函数只做转发，无第二套写出逻辑
         *  @note 没有构建任何值（root() 为 null）时写出 4 字节 "null"，与序列化一个 null 值等价
         */
        std::size_t serialize(char* out, std::size_t capacity, const JsonWriteOptions& options = {},
                              JsonWriteError* err = nullptr) const noexcept;

        /**
         *  @brief 把构建结果序列化成文本写进 std::string
         *  @param out 目标字符串；成功时被改写为恰好容纳结果的文本（不含结尾 NUL）
         *  @param options 缩进选项，语义与 `IDJsonWriter.hpp` 一致
         *  @return 成功时返回写入的字节数；失败时返回 0 且 out 不被修改
         *  @note 失败原因（非有限数、嵌套过深）不通过本重载返回：需要区分原因时用缓冲重载并传 err
         *  @note out 的扩容由标准库完成，分配失败会抛 std::bad_alloc（标准库行为，本函数自身不抛异常）；
         *        需要绝不抛异常的路径请用缓冲重载
         */
        std::size_t serialize(std::string& out, const JsonWriteOptions& options = {}) const;

    private:
        // 打开中的容器帧：元素类型是本库的私有布局（定义在实现文件），这里只保存 arena 上的地址
        struct Frame;

        Frame* frame_stack() noexcept;
        const Frame* frame_stack() const noexcept;

        JsonBuildError fail(JsonBuildError code) noexcept;
        JsonBuildError open_container(bool is_object) noexcept;
        JsonBuildError check_value_slot() const noexcept;
        JsonBuildError commit_value(const JsonValue& value) noexcept;

        JsonDocument   m_document;                                  // 构建产物的存储来源
        JsonValue      m_root{};                                    // 根值；未构建或失败时为 null
        void*          m_frames{nullptr};                           // arena 上的帧栈（Frame 数组）
        std::uint32_t  m_frame_count{0};                            // 已打开的容器数
        JsonBuildError m_error{JsonBuildError::none};               // 首个错误（粘性）
        bool           m_has_root{false};                           // 根值是否已写入
    };

    /**
     *  @brief 取构建错误码的稳定文本
     *  @param error 错误码
     *  @return 形如 "no_pending_key" 的静态字符串；未知取值返回 "unknown_error"
     */
    ID_JSON_API const char* to_string(JsonBuildError error) noexcept;
} // namespace ID::Json
