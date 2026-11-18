#include "project/Resolve/Generation.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Text.hpp"
#include "project/Input/Linkage.hpp"

/**
 *  三个规范化编码。
 *
 *  所有字段一律经 `update_field`（u64 小端长度 + 原始字节）与 `update_tag`（域分隔标签）追加，
 *  因此同一段数据放在不同语义位置时摘要不同，相邻字段也不会因切分不同而碰撞。
 */

namespace ID::Project
{
    namespace
    {
        // (path, fragment) 的排序键：fragment 为空的普通文件条目排在同名片段条目之前
        bool is_before(const ContentDigest& left, const ContentDigest& right) noexcept
        {
            if (left.path != right.path)
            {
                return std::string_view(left.path) < std::string_view(right.path);
            }
            if (left.fragment.has_value() != right.fragment.has_value())
            {
                return !left.fragment.has_value();
            }
            if (!left.fragment.has_value())
            {
                return false;
            }
            return std::string_view(*left.fragment) < std::string_view(*right.fragment);
        }

        void append_entries(Sha256& hash, std::span<const ContentDigest> entries, std::string_view tag)
        {
            std::vector<const ContentDigest*> sorted;
            sorted.reserve(entries.size());
            for (const ContentDigest& entry : entries)
            {
                sorted.push_back(&entry);
            }
            std::sort(sorted.begin(), sorted.end(),
                      [](const ContentDigest* left, const ContentDigest* right) noexcept
                      {
                          return is_before(*left, *right);
                      });

            for (const ContentDigest* entry : sorted)
            {
                hash.update_tag(tag);
                hash.update_field(entry->path);
                hash.update_field(entry->fragment.has_value() ? *entry->fragment : std::string());
            }
        }
    } // namespace

    Digest compute_selection_digest(const EffectiveSelection& selection)
    {
        std::vector<const EffectivePlugin*> plugins;
        plugins.reserve(selection.all.size());
        for (const EffectivePlugin& plugin : selection.all)
        {
            plugins.push_back(&plugin);
        }
        std::sort(plugins.begin(), plugins.end(),
                  [](const EffectivePlugin* left, const EffectivePlugin* right) noexcept
                  {
                      return std::string_view(left->name) < std::string_view(right->name);
                  });

        Sha256 hash;
        hash.update_tag("id-project-selection");
        for (const EffectivePlugin* plugin : plugins)
        {
            hash.update_tag("plugin");
            hash.update_field(plugin->name);
            hash.update_field(to_string(plugin->linkage));
            hash.update_field(plugin->is_direct ? std::string_view("direct") : std::string_view("dependency"));
            hash.update_field(to_decimal(plugin->options.size()));

            std::vector<const EffectiveOption*> options;
            options.reserve(plugin->options.size());
            for (const EffectiveOption& option : plugin->options)
            {
                options.push_back(&option);
            }
            std::sort(options.begin(), options.end(),
                      [](const EffectiveOption* left, const EffectiveOption* right) noexcept
                      {
                          return std::string_view(left->name) < std::string_view(right->name);
                      });

            for (const EffectiveOption* option : options)
            {
                hash.update_field(option->name);
                hash.update_field(option->value ? std::string_view("ON") : std::string_view("OFF"));
            }
        }
        return hash.finish();
    }

    std::string compute_generation_id(const GenerationContent& content)
    {
        std::vector<const std::pair<std::string, Digest>*> manifests;
        manifests.reserve(content.manifest_digests.size());
        for (const std::pair<std::string, Digest>& entry : content.manifest_digests)
        {
            manifests.push_back(&entry);
        }
        std::sort(manifests.begin(), manifests.end(),
                  [](const std::pair<std::string, Digest>* left, const std::pair<std::string, Digest>* right) noexcept
                  {
                      return std::string_view(left->first) < std::string_view(right->first);
                  });

        std::vector<const ContentDigest*> delivered;
        delivered.reserve(content.delivered.size());
        for (const ContentDigest& entry : content.delivered)
        {
            delivered.push_back(&entry);
        }
        std::sort(delivered.begin(), delivered.end(),
                  [](const ContentDigest* left, const ContentDigest* right) noexcept
                  {
                      return is_before(*left, *right);
                  });

        Sha256 hash;
        hash.update_tag("id-project-generation");
        hash.update_field(content.rules_version);
        hash.update_field(content.project_name);
        hash.update_field(content.selection_digest.to_hex());

        for (const std::pair<std::string, Digest>* manifest : manifests)
        {
            hash.update_tag("manifest");
            hash.update_field(manifest->first);
            hash.update_field(manifest->second.to_hex());
        }

        for (const ContentDigest* entry : delivered)
        {
            if (entry->fragment.has_value())
            {
                hash.update_tag("entry/fragment");
                hash.update_field(*entry->fragment);
            }
            else
            {
                hash.update_tag("entry/file");
            }
            hash.update_field(entry->path);
            hash.update_field(entry->digest.to_hex());
        }

        return hash.finish().to_hex();
    }

    Digest compute_plan_digest(std::span<const ContentDigest> writes, std::span<const ContentDigest> deletes)
    {
        Sha256 hash;
        hash.update_tag("id-project-plan");
        append_entries(hash, writes, std::string_view("write"));
        append_entries(hash, deletes, std::string_view("delete"));
        return hash.finish();
    }
} // namespace ID::Project
