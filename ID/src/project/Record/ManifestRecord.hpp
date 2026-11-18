#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "project/Base/Digest.hpp"

/**
 *  `manifest.json`：工具可覆盖路径清单（spec §6.3）。
 *
 *  条目身份是 `(path, fragment)` 唯一键：`(path, null)`、`(path, "output-dirs")`、
 *  `(path, "plugin-targets")` 是三条独立条目，各有独立摘要。
 *
 *  不变量：
 *  1. 片段条目的 `digest` 只能由 `ID::CMake::fragment_range_digest(file_bytes, range)` 计算，
 *     `range` 取自 `ID::CMake::parse_fragments`；不得自行拼接片段文本再哈希，也不得用整文件摘要顶替。
 *  2. `entries_digest` 是条目集合摘要（跨 generation 比较用），与 `resolved.manifest_digest`
 *     （实际写出的 manifest 字节摘要）用途不同，不可互换。
 *  3. `seed` 条目只记录首次创建时的摘要，之后不参与校验、不覆盖、不删除。
 *  4. 条目按 `(path, fragment)` 排序；同一对出现两次即记录非法（record_invalid）。
 */

namespace ID::Project
{
    /**
     *  条目来源种类。
     */
    enum class ManifestKind : std::uint8_t
    {
        Copied    = 0,   // "copied"：从插件源复制
        Generated = 1,   // "generated"：工具生成
        Seed      = 2,   // "seed"：工具首次创建后归用户
    };

    /**
     *  条目写入阶段（与 build 记录的 `stage` 同名不同义）。
     */
    enum class ManifestStage : std::uint8_t
    {
        Copy     = 0,   // "copy"
        Generate = 1,   // "generate"
    };

    /**
     *  一条受管路径。
     */
    struct ManifestEntry
    {
        std::string                path          = {};
        std::optional<std::string> fragment      = {};                        // 空：整文件；否则片段名
        ManifestKind               kind          = ManifestKind::Copied;
        std::optional<std::string> plugin        = {};
        std::optional<std::string> source        = {};                        // 相对工程根；生成物与 seed 为空
        std::string                digest        = {};                        // 整文件或片段文本范围的摘要
        ManifestStage              stage         = ManifestStage::Copy;
        std::string                generation_id = {};                        // 与所属 resolved 相同
    };

    /**
     *  `manifest.json` 的完整内容（键序与写出顺序见 ManifestJson.cpp）。
     */
    struct ManifestRecord
    {
        std::string                 schema_version = {};
        std::string                 generation_id  = {};
        std::uint64_t               entry_count    = 0;
        std::string                 entries_digest = {};   // 条目集合摘要
        std::vector<ManifestEntry>  entries        = {};   // 按 (path, fragment) 排序
    };

    /**
     *  @brief manifest 条目集合摘要
     *  @param entries 条目集合（内部按 (path, fragment) 排序后参与）
     *  @return 摘要值
     *  @note 编码：tag "id-project-manifest-entries"；对每个条目 tag "entry"、field(path)、
     *        fragment 为空时 tag "file" 否则 tag "fragment" + field(fragment)、field(kind 文本)、
     *        field(digest 十六进制)、field(stage 文本)、field(plugin 或空串)、field(source 或空串)
     *  @note 不含 generation_id（已由记录顶层承担）
     */
    Digest compute_entries_digest(std::span<const ManifestEntry> entries);
} // namespace ID::Project
