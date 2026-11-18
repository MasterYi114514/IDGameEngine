#include "cmake/File/FragmentLayout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ID::CMake
{
    namespace
    {
        constexpr std::size_t   kUtf8BomSize = 3;    // 既有 BOM 只识别不求值：属标记外字节，原样保留
        constexpr std::size_t   kSha256Block = 64;   // SHA-256 的分组长度（字节）
        constexpr std::size_t   kSha256PadTo = 56;   // 填充后留给位长字段的位置
        constexpr std::size_t   kSha256Words = 8;    // 摘要状态字数

        // SHA-256 轮常量与初始状态（FIPS 180-4）：片段摘要与装配层记录摘要必须同算法，不引入外部依赖
        constexpr std::uint32_t kSha256RoundConstants[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };

        constexpr std::uint32_t kSha256InitialState[kSha256Words] = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        };

        std::uint32_t rotate_right(std::uint32_t value, std::uint32_t bits) noexcept
        {
            return (value >> bits) | (value << (32u - bits));
        }

        // 一个 64 字节分组的压缩函数：状态原地更新，不分配内存
        void sha256_compress(std::uint32_t state[kSha256Words], const std::uint8_t* block) noexcept
        {
            std::uint32_t schedule[64] = {};

            for (std::size_t index = 0; index < 16; ++index)
            {
                schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24)
                                | (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16)
                                | (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8)
                                | static_cast<std::uint32_t>(block[index * 4 + 3]);
            }
            for (std::size_t index = 16; index < 64; ++index)
            {
                const std::uint32_t low_0 = rotate_right(schedule[index - 15], 7) ^ rotate_right(schedule[index - 15], 18)
                                          ^ (schedule[index - 15] >> 3);
                const std::uint32_t low_1 = rotate_right(schedule[index - 2], 17) ^ rotate_right(schedule[index - 2], 19)
                                          ^ (schedule[index - 2] >> 10);
                schedule[index] = schedule[index - 16] + low_0 + schedule[index - 7] + low_1;
            }

            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];
            std::uint32_t f = state[5];
            std::uint32_t g = state[6];
            std::uint32_t h = state[7];

            for (std::size_t index = 0; index < 64; ++index)
            {
                const std::uint32_t high_1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
                const std::uint32_t choose = (e & f) ^ ((~e) & g);
                const std::uint32_t temp_1 = h + high_1 + choose + kSha256RoundConstants[index] + schedule[index];
                const std::uint32_t high_0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
                const std::uint32_t major  = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t temp_2 = high_0 + major;

                h = g;
                g = f;
                f = e;
                e = d + temp_1;
                d = c;
                c = b;
                b = a;
                a = temp_1 + temp_2;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        // 摘要文本：小写十六进制，64 个字符；输入按 64 字节分组流式处理，不复制除末尾填充以外的字节
        std::string sha256_hex(std::string_view data)
        {
            std::uint32_t state[kSha256Words] = {};
            for (std::size_t index = 0; index < kSha256Words; ++index)
            {
                state[index] = kSha256InitialState[index];
            }

            std::uint8_t block[kSha256Block] = {};
            std::size_t  filled              = 0;
            for (const char unit : data)
            {
                block[filled] = static_cast<std::uint8_t>(unit);
                ++filled;
                if (filled == kSha256Block)
                {
                    sha256_compress(state, block);
                    filled = 0;
                }
            }

            // 填充：0x80 之后补 0，最后 8 字节写大端位长，使总长是 64 的整数倍
            const std::uint64_t total_bits = static_cast<std::uint64_t>(data.size()) * 8u;
            block[filled] = 0x80u;
            ++filled;
            if (filled > kSha256PadTo)
            {
                while (filled < kSha256Block)
                {
                    block[filled] = 0;
                    ++filled;
                }
                sha256_compress(state, block);
                filled = 0;
            }
            while (filled < kSha256PadTo)
            {
                block[filled] = 0;
                ++filled;
            }
            for (std::size_t index = 0; index < 8; ++index)
            {
                block[kSha256PadTo + index] = static_cast<std::uint8_t>((total_bits >> (56 - index * 8)) & 0xFFu);
            }
            sha256_compress(state, block);

            constexpr char kHexDigits[] = "0123456789abcdef";

            std::string text;
            text.reserve(kSha256Words * 8);
            for (const std::uint32_t word : state)
            {
                for (std::size_t shift = 0; shift < 8; ++shift)
                {
                    text.push_back(kHexDigits[(word >> (28 - shift * 4)) & 0x0Fu]);
                }
            }
            return text;
        }

        std::size_t skip_utf8_bom(std::string_view text) noexcept
        {
            if (text.size() >= kUtf8BomSize && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
            {
                return kUtf8BomSize;
            }
            return 0;
        }

        // 文件第一个换行符决定的行尾：单行文件没有既有行尾，按 '\n' 写
        std::string detect_newline(std::string_view text) noexcept
        {
            const std::size_t body_start = skip_utf8_bom(text);
            const std::size_t newline    = text.find('\n', body_start);
            if (newline == std::string_view::npos)
            {
                return "\n";
            }
            if (newline > body_start && text[newline - 1] == '\r')
            {
                return "\r\n";
            }
            return "\n";
        }

        // 一行标记的范围：first_line 是首行（BEGIN 标记行，含其换行），last_line 是末行（END 标记行及其后换行）
        struct RangeLines
        {
            std::string_view first_line = { };
            std::string_view last_line  = { };
        };

        // 从片段范围字节里切出两行标记：中间的全部字节（含换行）就是片段体，属调用方维护的内容
        RangeLines split_range_lines(std::string_view range_bytes) noexcept
        {
            RangeLines lines;

            const std::size_t first_newline = range_bytes.find('\n');
            if (first_newline == std::string_view::npos)
            {
                // 只有一个不换行的行：BEGIN 与 END 不可能同行（前缀不同），按空片段体处理
                lines.first_line = range_bytes;
                return lines;
            }
            lines.first_line = range_bytes.substr(0, first_newline + 1);

            std::size_t last_line_end = range_bytes.size();
            if (last_line_end > 0 && range_bytes[last_line_end - 1] == '\n')
            {
                --last_line_end;
            }

            std::size_t last_line_start = lines.first_line.size();
            for (std::size_t index = last_line_end; index > lines.first_line.size(); --index)
            {
                if (range_bytes[index - 1] == '\n')
                {
                    last_line_start = index;
                    break;
                }
            }
            if (last_line_start < last_line_end)
            {
                lines.last_line = range_bytes.substr(last_line_start);
            }
            else
            {
                // END 标记行紧跟在 BEGIN 标记行之后（空片段体）：末行从 BEGIN 行之后开始
                lines.last_line = range_bytes.substr(lines.first_line.size());
            }
            return lines;
        }

        // 片段体按文件既有行尾写出；内容里的 CRLF 按一次换行处理，结尾补一个换行以便 END 标记行独立成行
        void append_fragment_body(std::string& result, std::string_view body, std::string_view newline)
        {
            bool        ends_with_newline = body.empty();
            std::size_t index             = 0;
            while (index < body.size())
            {
                const char unit = body[index];
                if (unit == '\r' && index + 1 < body.size() && body[index + 1] == '\n')
                {
                    ++index;   // CRLF 里的 CR 已由 '\n' 分支写出，跳过以免写出 CRCRLF
                    continue;
                }
                if (unit == '\n')
                {
                    result.append(newline);
                    ends_with_newline = true;
                }
                else
                {
                    result.push_back(unit);
                    ends_with_newline = false;
                }
                ++index;
            }

            if (!body.empty() && !ends_with_newline)
            {
                result.append(newline);
            }
        }

        std::string make_marker_line(std::string_view prefix, std::string_view name)
        {
            std::string line(prefix);
            line.append(name);
            return line;
        }

        // 片段体替换后的一整段：BEGIN 标记行原样 + 新片段体 + END 标记行原样
        void append_replaced_range(std::string& result,
                                   std::string_view range_bytes,
                                   std::string_view body,
                                   std::string_view newline)
        {
            const RangeLines lines = split_range_lines(range_bytes);

            result.append(lines.first_line);
            append_fragment_body(result, body, newline);
            result.append(lines.last_line);
        }
    } // namespace

    FragmentLayout parse_fragments(std::string_view file_bytes, std::span<const std::string_view> names)
    {
        FragmentLayout layout;
        layout.ranges.reserve(names.size());

        for (const std::string_view name : names)
        {
            const std::string begin_marker = make_marker_line(kFragmentBeginPrefix, name);
            const std::string end_marker   = make_marker_line(kFragmentEndPrefix, name);

            std::size_t begin_count = 0;
            std::size_t begin_start = 0;
            std::size_t begin_next  = 0;
            std::size_t begin_line  = 0;
            std::size_t end_count   = 0;
            std::size_t end_start   = 0;
            std::size_t end_next    = 0;
            std::size_t end_line    = 0;

            std::size_t index       = skip_utf8_bom(file_bytes);
            std::size_t line_number = 1;
            while (index < file_bytes.size())
            {
                const std::size_t newline    = file_bytes.find('\n', index);
                const std::size_t line_end   = (newline == std::string_view::npos) ? file_bytes.size() : newline;
                const std::size_t next_start = (newline == std::string_view::npos) ? file_bytes.size() : newline + 1;

                std::string_view line = file_bytes.substr(index, line_end - index);
                if (!line.empty() && line.back() == '\r')
                {
                    line.remove_suffix(1);
                }

                // 两个标记各自独立计数：同一行同时等于两种标记时留给顺序判定报 Ambiguous
                if (line == begin_marker)
                {
                    ++begin_count;
                    begin_start = index;
                    begin_next  = next_start;
                    begin_line  = line_number;
                }
                if (line == end_marker)
                {
                    ++end_count;
                    end_start = index;
                    end_next  = next_start;
                    end_line  = line_number;
                }

                index = next_start;
                ++line_number;
            }

            if (begin_count == 0 && end_count == 0)
            {
                layout.state  = FragmentState::Missing;
                layout.detail = "片段 " + std::string(name) + " 的 BEGIN 与 END 标记都缺失";
                layout.ranges.clear();
                return layout;
            }
            if (begin_count > 1 || end_count > 1)
            {
                layout.state  = FragmentState::Ambiguous;
                layout.detail = "片段 " + std::string(name) + " 的标记重复：BEGIN 命中 " + std::to_string(begin_count) +
                                " 次，END 命中 " + std::to_string(end_count) + " 次";
                layout.ranges.clear();
                return layout;
            }
            if (begin_count == 0 || end_count == 0)
            {
                layout.state  = FragmentState::Incomplete;
                layout.detail = "片段 " + std::string(name) + " 只命中一半标记：BEGIN " + std::to_string(begin_count) +
                                " 次，END " + std::to_string(end_count) + " 次";
                layout.ranges.clear();
                return layout;
            }
            if (!(begin_start < end_start))
            {
                layout.state  = FragmentState::Ambiguous;
                layout.detail = "片段 " + std::string(name) + " 的 BEGIN 在第 " + std::to_string(begin_line) +
                                " 行、END 在第 " + std::to_string(end_line) + " 行：BEGIN 必须在 END 之前";
                layout.ranges.clear();
                return layout;
            }

            FragmentRange range;
            range.name   = std::string(name);
            range.offset = begin_start;
            range.length = end_next - begin_start;
            range.bytes  = std::string(file_bytes.substr(range.offset, range.length));
            layout.ranges.push_back(std::move(range));
        }

        std::sort(layout.ranges.begin(),
                  layout.ranges.end(),
                  [](const FragmentRange& lhs, const FragmentRange& rhs) noexcept { return lhs.offset < rhs.offset; });

        for (std::size_t index = 1; index < layout.ranges.size(); ++index)
        {
            const FragmentRange& previous = layout.ranges[index - 1];
            const FragmentRange& current  = layout.ranges[index];
            if (current.offset < previous.offset + previous.length)
            {
                layout.state  = FragmentState::OutOfOrder;
                layout.detail = "片段 " + previous.name + " 与 " + current.name + " 的文本范围重叠";
                layout.ranges.clear();
                return layout;
            }
        }

        layout.state  = FragmentState::Ok;
        layout.detail = { };
        return layout;
    }

    std::optional<std::string> compose_fragments(
        std::string_view file_bytes,
        const FragmentLayout& layout,
        std::span<const std::pair<std::string_view, std::string_view>> replacements)
    {
        if (layout.state != FragmentState::Ok)
        {
            return std::nullopt;
        }

        const std::string newline = detect_newline(file_bytes);

        std::string result;
        result.reserve(file_bytes.size());

        std::size_t cursor = 0;
        for (const FragmentRange& range : layout.ranges)
        {
            // 范围必须与本次给出的字节同源：偏移回退或长度越界都说明布局来自另一份字节
            if (range.offset < cursor || range.offset > file_bytes.size()
                || range.length > file_bytes.size() - range.offset)
            {
                return std::nullopt;
            }

            const std::string_view* replacement = nullptr;
            for (const std::pair<std::string_view, std::string_view>& candidate : replacements)
            {
                if (candidate.first == range.name)
                {
                    replacement = &candidate.second;
                    break;
                }
            }
            if (replacement == nullptr)
            {
                // 缺某片段的新内容：不猜、不保留旧内容，整体放弃
                return std::nullopt;
            }

            result.append(file_bytes.substr(cursor, range.offset - cursor));
            append_replaced_range(result, range.bytes, *replacement, newline);
            cursor = range.offset + range.length;
        }
        result.append(file_bytes.substr(cursor));

        return result;
    }

    std::string compose_new_root_file(std::string_view header,
                                      std::string_view output_dirs_body,
                                      std::string_view plugin_targets_body)
    {
        // 首次创建由本工具定义整文件：行尾固定 '\n'，片段之间与 header 之后各一个空行（这些空行在片段
        // 范围之外，之后的片段替换不动它们）
        constexpr std::string_view kNewline = "\n";

        std::string result;
        result.reserve(header.size() + output_dirs_body.size() + plugin_targets_body.size() + 256);

        if (!header.empty())
        {
            result.append(header);
            if (result.back() != '\n')
            {
                result.append(kNewline);
            }
            result.append(kNewline);
        }

        // 顺序固定为输出目录片段在前、插件目标片段在后：两段合起来就是根文件的正文
        const std::pair<std::string_view, std::string_view> fragments[]{
            {kFragmentOutputDirs, output_dirs_body},
            {kFragmentPluginTargets, plugin_targets_body},
        };
        constexpr std::size_t kFragmentCount = 2;

        for (std::size_t index = 0; index < kFragmentCount; ++index)
        {
            if (index > 0)
            {
                result.append(kNewline);
            }

            const std::string begin_marker = make_marker_line(kFragmentBeginPrefix, fragments[index].first);
            const std::string end_marker   = make_marker_line(kFragmentEndPrefix, fragments[index].first);

            result.append(begin_marker);
            result.append(kNewline);
            append_fragment_body(result, fragments[index].second, kNewline);
            result.append(end_marker);
            result.append(kNewline);
        }

        return result;
    }

    std::string fragment_range_digest(std::string_view file_bytes, const FragmentRange& range)
    {
        if (range.length == 0 || range.offset > file_bytes.size() || range.length > file_bytes.size() - range.offset)
        {
            return std::string();
        }
        return sha256_hex(file_bytes.substr(range.offset, range.length));
    }
} // namespace ID::CMake
