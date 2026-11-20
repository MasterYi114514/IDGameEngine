#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#include "Foundation/Sha256.hpp"
#include "Foundation/Utf8.hpp"
#include "Knowbe/Version.hpp"

/**
 *  Knowbe core 链接探测夹具的入口（P0 seam）：只证明"core 能被普通 CMake 宿主链接并运行"。
 *
 *  调用点选 core 已实现的最小 Foundation 能力（SHA-256 与 UTF-8 合法性），断言值取公开的标准测试向量
 *  （SHA-256("abc")）与显然的非法序列（过长编码），因此输出可被命令行逐行断言；版本常量同时打印，
 *  用于确认取到的是同一份公开头。
 *
 *  边界：本夹具不写官方报告、不参与评级、不做性能测量；它也不进入 Benchmark 官方可执行文件。
 *  Foundation 头位于 core/src（PRIVATE include），夹具显式补一条 include 根来调用，这不改变 core 的对外契约。
 */

namespace
{
    // SHA-256("abc") 的标准测试向量（FIPS 180-2 附录 B）
    constexpr std::string_view kAbcText        = "abc";
    constexpr std::string_view kAbcSha256Hex   = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    // `C0 AF` 是 ASCII '/' 的过长编码，必须被判为非法 UTF-8
    constexpr std::string_view kOverlongUtf8 = "\xC0\xAF";

    // TU 私有的失败计数：只在 main 内累加与读取，不交出地址
    int g_failure_count = 0;

    void check(bool ok, const char* name) noexcept
    {
        std::printf("[PROBE] %s = %d\n", name, ok ? 1 : 0);
        if (!ok)
        {
            ++g_failure_count;
        }
    }
} // namespace

int main()
{
    std::printf("[PROBE] knowbe.format_version = %u\n", static_cast<unsigned>(Knowbe::kFormatVersion));
    std::printf("[PROBE] knowbe.chunk_spec_version = %u\n", static_cast<unsigned>(Knowbe::kChunkSpecVersion));
    std::printf("[PROBE] knowbe.digest_size = %u\n", static_cast<unsigned>(Knowbe::kDigestSize));

    bool is_sha_ok = false;
    const std::optional<Knowbe::Foundation::Digest> digest =
        Knowbe::Foundation::sha256(std::as_bytes(std::span<const char>(kAbcText.data(), kAbcText.size())));
    if (digest.has_value())
    {
        std::array<char, Knowbe::Foundation::kHexDigestSize> hex = {};
        Knowbe::Foundation::digest_to_hex_lower(*digest, std::span<char, Knowbe::Foundation::kHexDigestSize>(hex));
        const std::string_view hex_text(hex.data(), hex.size());
        std::printf("[PROBE] sha256_abc = %.*s\n", static_cast<int>(hex_text.size()), hex_text.data());
        is_sha_ok = hex_text == kAbcSha256Hex;
    }
    check(is_sha_ok, "sha256_abc_matches_vector");

    check(Knowbe::Foundation::is_valid_utf8("knowbe"), "utf8_ascii_accepted");
    check(!Knowbe::Foundation::is_valid_utf8(kOverlongUtf8), "utf8_overlong_rejected");

    std::printf("[PROBE] result = %s\n", g_failure_count == 0 ? "PASS" : "FAIL");
    return g_failure_count == 0 ? 0 : 1;
}
