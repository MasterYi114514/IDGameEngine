#include "project/Base/Digest.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>

#include "Core/Core.hpp"

#include "project/Base/Text.hpp"

/**
 *  SHA-256 实现（FIPS 180-4）与摘要对象的编解码。
 *
 *  分组缓冲与累计长度都由本类持有：update 只做定长拷贝、满一组就压缩，因此文件与规范化文本
 *  可以共用同一台机器，不需要调用方先拼出完整输入。
 *  规范化编码只有一处定义：update_field 写 u64 小端长度 + 原始字节，update_tag 先写一个标签
 *  标记字节再按字段编码写标签文本。
 */

namespace ID::Project
{
    namespace
    {
        inline constexpr std::size_t kBlockBytes     = 64;          // SHA-256 分组长度
        inline constexpr std::size_t kLengthBytes    = 8;           // 末尾位长字段长度
        inline constexpr std::size_t kFileBufferBytes = 4u << 20;   // 文件摘要的流式缓冲
        inline constexpr std::uint8_t kTagMarkerByte = 0x01;        // 域分隔标签的标记字节

        // FIPS 180-4 §4.2.2 的 64 个轮常量
        inline constexpr std::uint32_t kRoundConstants[64] =
        {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };

        // FIPS 180-4 §5.3.3 的初始状态
        inline constexpr std::uint32_t kInitialState[8] =
        {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        };

        ID_FORCEINLINE std::uint32_t rotate_right(std::uint32_t value, std::uint32_t bits) noexcept
        {
            return (value >> bits) | (value << (32u - bits));
        }

        // 十六进制字符转取值：非法字符返回 0xFF
        ID_FORCEINLINE std::uint8_t hex_value(char unit) noexcept
        {
            if (unit >= '0' && unit <= '9')
            {
                return static_cast<std::uint8_t>(unit - '0');
            }
            if (unit >= 'a' && unit <= 'f')
            {
                return static_cast<std::uint8_t>(unit - 'a' + 10);
            }
            if (unit >= 'A' && unit <= 'F')
            {
                return static_cast<std::uint8_t>(unit - 'A' + 10);
            }
            return 0xFF;
        }
    } // namespace

    std::string Digest::to_hex() const
    {
        return to_lower_hex(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    }

    bool Digest::is_zero() const noexcept
    {
        for (const std::uint8_t byte : bytes)
        {
            if (byte != 0)
            {
                return false;
            }
        }
        return true;
    }

    bool digest_from_hex(std::string_view hex, Digest& out) noexcept
    {
        if (hex.size() != kDigestHexLength)
        {
            return false;
        }

        Digest parsed;
        for (std::size_t index = 0; index < kDigestBytes; ++index)
        {
            const std::uint8_t high = hex_value(hex[index * 2]);
            const std::uint8_t low  = hex_value(hex[index * 2 + 1]);
            if (high == 0xFF || low == 0xFF)
            {
                return false;   // 失败时 out 保持原值
            }
            parsed.bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
        }

        out = parsed;
        return true;
    }

    Sha256::Sha256() noexcept
    {
        for (std::size_t index = 0; index < m_state.size(); ++index)
        {
            m_state[index] = kInitialState[index];
        }
    }

    void Sha256::update(std::span<const std::uint8_t> bytes) noexcept
    {
        m_total_bytes += bytes.size();

        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const std::size_t remaining = bytes.size() - offset;
            const std::size_t space     = m_buffer.size() - m_buffer_length;
            const std::size_t copied    = remaining < space ? remaining : space;

            std::memcpy(m_buffer.data() + m_buffer_length, bytes.data() + offset, copied);
            m_buffer_length += copied;
            offset += copied;

            if (m_buffer_length == m_buffer.size())
            {
                compress(m_buffer.data());
                m_buffer_length = 0;
            }
        }
    }

    void Sha256::update(std::string_view text) noexcept
    {
        update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    void Sha256::update_field(std::string_view text) noexcept
    {
        update_field(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    void Sha256::update_field(std::span<const std::uint8_t> bytes) noexcept
    {
        // u64 小端长度前缀：字段边界不依赖内容，拼接的切分方式不同即摘要不同
        std::uint8_t prefix[kLengthBytes] = {};
        const std::uint64_t length = bytes.size();
        for (std::size_t index = 0; index < kLengthBytes; ++index)
        {
            prefix[index] = static_cast<std::uint8_t>((length >> (8 * index)) & 0xFFu);
        }

        update(std::span<const std::uint8_t>(prefix, kLengthBytes));
        update(bytes);
    }

    void Sha256::update_tag(std::string_view tag) noexcept
    {
        update(std::span<const std::uint8_t>(&kTagMarkerByte, 1));
        update_field(tag);
    }

    Digest Sha256::finish() noexcept
    {
        // 位长必须在填充前记录：填充会改变累计字节数
        const std::uint64_t bit_length = m_total_bytes * 8;

        const std::uint8_t marker = 0x80;
        update(std::span<const std::uint8_t>(&marker, 1));

        const std::uint8_t zeros[kBlockBytes] = {};
        while (m_buffer_length != kBlockBytes - kLengthBytes)
        {
            const std::size_t needed = m_buffer_length < kBlockBytes - kLengthBytes
                                           ? (kBlockBytes - kLengthBytes) - m_buffer_length
                                           : (kBlockBytes - m_buffer_length) + (kBlockBytes - kLengthBytes);
            update(std::span<const std::uint8_t>(zeros, needed));
        }

        std::uint8_t length_bytes[kLengthBytes] = {};
        for (std::size_t index = 0; index < kLengthBytes; ++index)
        {
            length_bytes[index] = static_cast<std::uint8_t>((bit_length >> (56 - 8 * index)) & 0xFFu);
        }
        update(std::span<const std::uint8_t>(length_bytes, kLengthBytes));

        Digest result;
        for (std::size_t index = 0; index < m_state.size(); ++index)
        {
            const std::uint32_t word = m_state[index];
            result.bytes[index * 4]     = static_cast<std::uint8_t>((word >> 24) & 0xFFu);
            result.bytes[index * 4 + 1] = static_cast<std::uint8_t>((word >> 16) & 0xFFu);
            result.bytes[index * 4 + 2] = static_cast<std::uint8_t>((word >> 8) & 0xFFu);
            result.bytes[index * 4 + 3] = static_cast<std::uint8_t>(word & 0xFFu);
        }
        return result;
    }

    void Sha256::compress(const std::uint8_t* block) noexcept
    {
        std::uint32_t schedule[64] = {};

        for (std::size_t index = 0; index < 16; ++index)
        {
            const std::size_t offset = index * 4;
            schedule[index] = (static_cast<std::uint32_t>(block[offset]) << 24)
                              | (static_cast<std::uint32_t>(block[offset + 1]) << 16)
                              | (static_cast<std::uint32_t>(block[offset + 2]) << 8)
                              | static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t index = 16; index < 64; ++index)
        {
            const std::uint32_t first  = schedule[index - 15];
            const std::uint32_t second = schedule[index - 2];
            const std::uint32_t s0     = rotate_right(first, 7) ^ rotate_right(first, 18) ^ (first >> 3);
            const std::uint32_t s1     = rotate_right(second, 17) ^ rotate_right(second, 19) ^ (second >> 10);
            schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
        }

        std::uint32_t a = m_state[0];
        std::uint32_t b = m_state[1];
        std::uint32_t c = m_state[2];
        std::uint32_t d = m_state[3];
        std::uint32_t e = m_state[4];
        std::uint32_t f = m_state[5];
        std::uint32_t g = m_state[6];
        std::uint32_t h = m_state[7];

        for (std::size_t index = 0; index < 64; ++index)
        {
            const std::uint32_t sum1     = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const std::uint32_t choice   = (e & f) ^ (~e & g);
            const std::uint32_t temp1    = h + sum1 + choice + kRoundConstants[index] + schedule[index];
            const std::uint32_t sum0     = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2    = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    Digest digest_bytes(std::span<const std::uint8_t> bytes) noexcept
    {
        Sha256 hasher;
        hasher.update(bytes);
        return hasher.finish();
    }

    Digest digest_text(std::string_view text) noexcept
    {
        Sha256 hasher;
        hasher.update(text);
        return hasher.finish();
    }

    Digest digest_file(const std::filesystem::path& path, bool& is_ok) noexcept
    {
        is_ok = false;

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return Digest{};
        }

        // 用 nothrow 分配：分配失败同样通过 is_ok 为假表达，不把本函数变成终止路径
        const std::unique_ptr<std::uint8_t[]> buffer(new (std::nothrow) std::uint8_t[kFileBufferBytes]);
        if (!buffer)
        {
            return Digest{};
        }

        Sha256 hasher;
        for (;;)
        {
            stream.read(reinterpret_cast<char*>(buffer.get()), static_cast<std::streamsize>(kFileBufferBytes));
            const std::streamsize read = stream.gcount();
            if (read > 0)
            {
                hasher.update(std::span<const std::uint8_t>(buffer.get(), static_cast<std::size_t>(read)));
            }
            if (static_cast<std::size_t>(read) < kFileBufferBytes)
            {
                break;
            }
        }

        if (stream.bad())
        {
            return Digest{};
        }

        is_ok = true;
        return hasher.finish();
    }
} // namespace ID::Project
