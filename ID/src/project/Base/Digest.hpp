#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

/**
 *  SHA-256（FIPS 180-4）与摘要对象：文件、规范化文本与记录字段共用同一个实现。
 *
 *  不引入三方库：自实现的 SHA-256 只依赖标准库，避免为一次摘要计算新增构建依赖
 *  （cpp-style 第 0 节第 5 条：新增三方依赖属于需要授权的方案决策）。
 *
 *  不变量：
 *  1. 摘要对象只存原始 32 字节；十六进制只在序列化边界生成，记录里一律写小写十六进制。
 *  2. 所有"规范化编码"（selection 摘要、generation 身份、manifest 条目集合摘要）只允许用
 *     update_field 组合，不允许把字符串拼接后再 update：拼接会让 "ab"+"c" 与 "a"+"bc" 得到同一摘要。
 *  3. is_zero() 不是合法的真实摘要，只表示"未计算"；写入记录前必须先确认非零。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    inline constexpr std::size_t kDigestBytes     = 32;
    inline constexpr std::size_t kDigestHexLength = 64;

    /**
     *  摘要值：32 字节原始摘要。纯数据载体，不带判定逻辑。
     */
    struct Digest
    {
        std::array<std::uint8_t, kDigestBytes> bytes = {};      // 原始摘要，全零表示未计算

        /**
         *  @brief 取小写十六进制文本
         *  @return 64 个字符的小写十六进制串，用于记录与诊断
         */
        std::string to_hex() const;

        /**
         *  @brief 是否为全零（未计算）
         *  @return 全部 32 字节为 0 时返回 true
         */
        bool is_zero() const noexcept;
    };

    /**
     *  @brief 解析 64 个字符的小写或大写十六进制摘要
     *  @param hex 十六进制文本
     *  @param out 解析结果；成功时被完全覆盖，失败时保持原值
     *  @return 长度恰为 64 且字符集合法时返回 true
     *  @note 出参形态由调用方复用固定缓冲的需要决定；失败语义是"保持原值"，调用方据返回值判断
     */
    bool digest_from_hex(std::string_view hex, Digest& out) noexcept;

    /**
     *  流式哈希器：不缓存整段输入，供文件与规范化文本共用。
     *  内部维护 64 字节分组缓冲与累计字节数，update 只做定长拷贝与按需压缩。
     */
    class Sha256
    {
    public:
        /**
         *  @brief 构造为 SHA-256 的初始状态（FIPS 180-4 §5.3.3）
         *  @note 不分配、不失败
         */
        Sha256() noexcept;

        /**
         *  @brief 追加原始字节
         *  @param bytes 输入字节，可为空
         *  @note 不分配、不抛异常
         */
        void update(std::span<const std::uint8_t> bytes) noexcept;

        /**
         *  @brief 追加文本的字节（按字节读，不做编码转换）
         *  @param text 输入文本
         */
        void update(std::string_view text) noexcept;

        /**
         *  @brief 追加一个带长度前缀的字段：u64 小端长度 + 原始字节
         *  @param text 字段内容
         *  @note 规范化编码的唯一定义：所有进入 generation 身份的字段都必须经此追加，
         *        以消除"拼接歧义"（"ab"+"c" 与 "a"+"bc" 不得同摘要）
         */
        void update_field(std::string_view text) noexcept;

        /**
         *  @brief 追加一个带长度前缀的字段（字节形态）
         *  @param bytes 字段内容
         */
        void update_field(std::span<const std::uint8_t> bytes) noexcept;

        /**
         *  @brief 追加一个域分隔标签
         *  @param tag 标签文本
         *  @note 编码为"1 个标签标记字节 + 按 update_field 编码的标签文本"：同一段数据放在不同语义
         *        位置时摘要不同，两个相邻标签也不会因为切分不同而碰撞
         */
        void update_tag(std::string_view tag) noexcept;

        /**
         *  @brief 结束计算并取摘要
         *  @return 本次输入的 SHA-256 摘要
         *  @note 会写入填充并压缩最后一组；调用后本对象内的状态不再表示"可继续追加的哈希"，
         *        需要新摘要时构造新对象
         */
        Digest finish() noexcept;

    private:
        void compress(const std::uint8_t* block) noexcept;

        std::array<std::uint32_t, 8>    m_state         = {};       // 中间状态 H0–H7
        std::array<std::uint8_t, 64>    m_buffer        = {};       // 未满一组的输入
        std::size_t                     m_buffer_length = 0;        // 缓冲内已有字节数
        std::uint64_t                   m_total_bytes   = 0;        // 已输入字节总数（用于末尾位长）
    };

    /**
     *  @brief 一次性计算字节串摘要
     *  @param bytes 输入字节
     */
    Digest digest_bytes(std::span<const std::uint8_t> bytes) noexcept;

    /**
     *  @brief 一次性计算文本摘要（按字节读）
     *  @param text 输入文本
     */
    Digest digest_text(std::string_view text) noexcept;

    /**
     *  @brief 计算文件内容摘要（流式，4 MiB 缓冲）
     *  @param path 文件路径
     *  @param is_ok 成功读取到文件末尾时置真；文件不存在、打开失败或读取中途失败时置假
     *  @return 成功时返回文件摘要；失败时返回 is_zero() 为真的摘要，由调用方按 I/O 失败处理
     *  @note 缓冲用 new (std::nothrow) 分配：分配失败同样通过 is_ok 为假表达，不终止进程
     *  @note 只读；不整文件读入内存（memory.md 第 9 节）
     */
    Digest digest_file(const std::filesystem::path& path, bool& is_ok) noexcept;
} // namespace ID::Project
