#include "project/Resolve/Closure.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"

/**
 *  闭包补齐、链接边合成与稳定拓扑排序。
 *
 *  边合成规则（spec §10.2，唯一口径）：
 *  1. `dependencies` 的每个上游先产生无条件边；
 *  2. 生效值为 ON 的选项，其 `links_when_on` 的每个上游若已有无条件边则替换为条件边；
 *  3. 同一条条件边被多个选项控制时合并成一条，`controlling_options` 按字节序；
 *  4. 生效值为 OFF 的选项不产生边。
 */

namespace ID::Project
{
    namespace
    {
        void sort_names(std::vector<std::string>& names)
        {
            std::sort(names.begin(), names.end(),
                      [](const std::string& left, const std::string& right) noexcept
                      {
                          return std::string_view(left) < std::string_view(right);
                      });
        }

        void sort_plugins(std::vector<EffectivePlugin>& plugins)
        {
            std::sort(plugins.begin(), plugins.end(),
                      [](const EffectivePlugin& left, const EffectivePlugin& right) noexcept
                      {
                          return std::string_view(left.name) < std::string_view(right.name);
                      });
        }

        const EffectivePlugin* find_effective(const std::vector<EffectivePlugin>& plugins,
                                              std::string_view name) noexcept
        {
            for (const EffectivePlugin& plugin : plugins)
            {
                if (std::string_view(plugin.name) == name)
                {
                    return &plugin;
                }
            }
            return nullptr;
        }

        ProjectDiagnostic make_plugin_unknown(const PluginCatalog& catalog, std::string_view name)
        {
            const UnknownPluginReason reason = explain_unknown(catalog, name);
            std::string actual = reason.is_ignored ? "该插件已被忽略" : "IDPlugin/ 下未被发现";
            if (!reason.original_reason.empty())
            {
                actual += "（原始原因：";
                actual += reason.original_reason;
                actual += "）";
            }

            return ProjectDiagnostic{ProjectCategory::PluginUnknown,
                                     std::string(name),
                                     std::string("闭包内的每个插件都有已发现且未被忽略的清单"),
                                     std::move(actual),
                                     std::string("IDPlugin"),
                                     std::string("补齐该依赖插件或修正上游的 dependencies 后重跑，"
                                                 "不把缺失上游移出闭包后继续")};
        }

        // 边合成的中间形态：无条件边与"条件边 → 控制选项集合"
        class EdgeBuilder
        {
        public:
            explicit EdgeBuilder(std::string from) noexcept : m_from(std::move(from)) {}

            void add_unconditional(std::string to)
            {
                m_unconditional.push_back(std::move(to));
            }

            void add_conditional(std::string to, std::string option)
            {
                for (ConditionalEdge& edge : m_conditional)
                {
                    if (edge.to != to)
                    {
                        continue;
                    }
                    edge.options.push_back(std::move(option));
                    return;
                }
                ConditionalEdge edge;
                edge.to = std::move(to);
                edge.options.push_back(std::move(option));
                m_conditional.push_back(std::move(edge));
            }

            std::vector<LinkEdge> take_edges()
            {
                std::vector<LinkEdge> edges;

                // 条件边先落地：条件命中的上游不再保留无条件边
                for (ConditionalEdge& edge : m_conditional)
                {
                    for (std::size_t index = 0; index < m_unconditional.size(); ++index)
                    {
                        if (m_unconditional[index] != edge.to)
                        {
                            continue;
                        }
                        m_unconditional.erase(m_unconditional.begin() + static_cast<std::ptrdiff_t>(index));
                        break;
                    }

                    sort_names(edge.options);
                    edge.options.erase(std::unique(edge.options.begin(), edge.options.end()), edge.options.end());

                    LinkEdge result;
                    result.from                = m_from;
                    result.to                  = edge.to;
                    result.is_conditional      = true;
                    result.controlling_options = std::move(edge.options);
                    edges.push_back(std::move(result));
                }

                for (std::string& to : m_unconditional)
                {
                    LinkEdge result;
                    result.from           = m_from;
                    result.to             = std::move(to);
                    result.is_conditional = false;
                    edges.push_back(std::move(result));
                }
                return edges;
            }

        private:
            struct ConditionalEdge
            {
                std::string              to      = {};
                std::vector<std::string> options = {};
            };

            std::string                 m_from          = {};
            std::vector<std::string>    m_unconditional = {};
            std::vector<ConditionalEdge> m_conditional  = {};
        };

        void sort_edges(std::vector<LinkEdge>& edges)
        {
            std::sort(edges.begin(), edges.end(),
                      [](const LinkEdge& left, const LinkEdge& right) noexcept
                      {
                          if (left.from != right.from)
                          {
                              return std::string_view(left.from) < std::string_view(right.from);
                          }
                          return std::string_view(left.to) < std::string_view(right.to);
                      });
        }

        // 闭包内一个插件的编译面事实：源目录、include 根（含 SYSTEM 判定）、共享 vendor 链接
        PluginCompileFacts make_compile_facts(const PluginManifest& manifest)
        {
            PluginCompileFacts facts;
            facts.plugin = manifest.name;

            for (const std::string& directory : manifest.source_dirs)
            {
                facts.source_dirs.push_back(manifest.source_root + "/" + directory);
            }
            sort_names(facts.source_dirs);

            for (const HeaderDirEntry& header : manifest.header_dirs)
            {
                if (header.kind != HeaderDirKind::Directory)
                {
                    continue;   // 单文件条目不新增 include 根
                }

                IncludeRoot root;
                root.path = manifest.source_root + "/" + header.path;
                for (const std::string& vendor : manifest.vendor_dirs)
                {
                    // 落在 vendor_dirs 条目之下：三方头，SYSTEM PUBLIC
                    if (is_under(header.path, vendor))
                    {
                        root.is_system = true;
                        break;
                    }
                }
                facts.include_roots.push_back(std::move(root));
            }
            std::sort(facts.include_roots.begin(), facts.include_roots.end(),
                      [](const IncludeRoot& left, const IncludeRoot& right) noexcept
                      {
                          return std::string_view(left.path) < std::string_view(right.path);
                      });

            for (const SharedVendor& vendor : manifest.shared_vendors)
            {
                if (!vendor.target.has_value())
                {
                    continue;   // 只复制、不接入构建
                }
                VendorLink link;
                link.dir    = vendor.dir;
                link.target = *vendor.target;
                facts.vendor_links.push_back(std::move(link));
            }
            std::sort(facts.vendor_links.begin(), facts.vendor_links.end(),
                      [](const VendorLink& left, const VendorLink& right) noexcept
                      {
                          if (left.dir != right.dir)
                          {
                              return std::string_view(left.dir) < std::string_view(right.dir);
                          }
                          return std::string_view(left.target) < std::string_view(right.target);
                      });

            return facts;
        }
    } // namespace

    TopoResult topological_order(std::span<const std::string> nodes, std::span<const LinkEdge> edges)
    {
        TopoResult result;

        std::vector<std::string> order(nodes.begin(), nodes.end());
        sort_names(order);

        const std::size_t count = order.size();
        auto index_of = [&order](std::string_view name) noexcept -> std::size_t
        {
            for (std::size_t index = 0; index < order.size(); ++index)
            {
                if (std::string_view(order[index]) == name)
                {
                    return index;
                }
            }
            return order.size();
        };

        // 依赖对（唯一化）：from 依赖 to，因此 to 必须先输出
        std::vector<std::pair<std::size_t, std::size_t>> pairs;
        pairs.reserve(edges.size());
        for (const LinkEdge& edge : edges)
        {
            const std::size_t from = index_of(edge.from);
            const std::size_t to   = index_of(edge.to);
            if (from == count || to == count || from == to)
            {
                continue;   // 闭包外的边与自环不进排序（自环由调用方的清单校验拒绝）
            }
            bool is_duplicate = false;
            for (const std::pair<std::size_t, std::size_t>& existing : pairs)
            {
                if (existing.first == from && existing.second == to)
                {
                    is_duplicate = true;
                    break;
                }
            }
            if (!is_duplicate)
            {
                pairs.emplace_back(from, to);
            }
        }

        std::vector<std::size_t> unmet(count, 0);
        for (const std::pair<std::size_t, std::size_t>& pair : pairs)
        {
            ++unmet[pair.first];
        }

        // 按层推进：先取出当前全部入度为 0 的节点作为一层，整层输出后再统一释放它们的出边。
        // order 已按名字排序，层内遍历顺序即字节序；这样本层未输出的节点不会被刚释放的下一层节点插队
        // （spec §10.3：被依赖者在前、同层按插件名码点升序）
        std::vector<bool>        emitted(count, false);
        std::vector<std::size_t> layer;
        result.order.reserve(count);
        while (result.order.size() < count)
        {
            layer.clear();
            for (std::size_t index = 0; index < count; ++index)
            {
                if (!emitted[index] && unmet[index] == 0)
                {
                    layer.push_back(index);
                }
            }

            if (layer.empty())
            {
                // 剩余节点成环：从最小未输出节点沿依赖走，直到重复出现
                std::size_t start = count;
                for (std::size_t index = 0; index < count; ++index)
                {
                    if (!emitted[index])
                    {
                        start = index;
                        break;
                    }
                }

                std::vector<std::size_t> path;
                std::size_t current = start;
                while (current != count)
                {
                    const auto found = std::find(path.begin(), path.end(), current);
                    if (found != path.end())
                    {
                        for (auto position = found; position != path.end(); ++position)
                        {
                            result.cycle.push_back(order[*position]);
                        }
                        break;
                    }
                    path.push_back(current);

                    std::size_t next = count;
                    for (const std::pair<std::size_t, std::size_t>& pair : pairs)
                    {
                        if (pair.first == current && !emitted[pair.second])
                        {
                            next = pair.second;
                            break;
                        }
                    }
                    current = next;
                }
                return result;
            }

            for (const std::size_t index : layer)
            {
                emitted[index] = true;
                result.order.push_back(order[index]);
            }

            // 整层输出后才释放出边：下一层只能在本层全部就位后进入候选集合
            for (const std::size_t index : layer)
            {
                for (const std::pair<std::size_t, std::size_t>& pair : pairs)
                {
                    if (pair.second == index && unmet[pair.first] > 0)
                    {
                        --unmet[pair.first];
                    }
                }
            }
        }

        result.is_ok = result.order.size() == count;
        return result;
    }

    Resolution resolve_closure(const EffectiveSelection& selection, const PluginCatalog& catalog)
    {
        Resolution resolution;
        resolution.selection = selection;

        if (!selection.is_valid)
        {
            resolution.diagnostics = selection.diagnostics;
            resolution.is_valid    = false;
            return resolution;
        }

        std::vector<EffectivePlugin> closure = selection.direct;
        sort_plugins(closure);

        // 闭包扩张：只沿 dependencies；补上的插件取清单缺省值
        for (std::size_t cursor = 0; cursor < closure.size(); ++cursor)
        {
            const std::string plugin_name = closure[cursor].name;
            const CatalogEntry* entry = find_plugin(catalog, plugin_name);
            if (entry == nullptr)
            {
                resolution.diagnostics.add(make_plugin_unknown(catalog, plugin_name));
                continue;
            }

            for (const std::string& dependency : entry->manifest.dependencies)
            {
                if (find_effective(closure, dependency) != nullptr)
                {
                    continue;   // 同一节点：直接项取值优先，不产生第二份配置
                }

                const CatalogEntry* upstream = find_plugin(catalog, dependency);
                if (upstream == nullptr)
                {
                    resolution.diagnostics.add(make_plugin_unknown(catalog, dependency));
                    continue;
                }
                closure.push_back(expand_manifest_defaults(upstream->manifest, false));
            }
        }
        sort_plugins(closure);

        // 补齐项：闭包里有、直接选择里没有的插件。下游取声明该依赖且字节序最小的插件，
        // 使同一输入得到同一条 info 说明（spec §10.1：补齐不是警告，也不改写用户文件）
        std::vector<ClosedBackPlugin> closed_back;
        for (const EffectivePlugin& plugin : closure)
        {
            const CatalogEntry* entry = find_plugin(catalog, plugin.name);
            if (entry == nullptr || entry->manifest.dependencies.empty())
            {
                continue;
            }

            std::vector<std::string> dependencies = entry->manifest.dependencies;
            sort_names(dependencies);
            for (const std::string& dependency : dependencies)
            {
                if (find_effective(selection.direct, dependency) != nullptr)
                {
                    continue;   // 直接选择项不是补齐
                }

                bool is_known = false;
                for (const ClosedBackPlugin& known : closed_back)
                {
                    if (known.plugin == dependency)
                    {
                        is_known = true;
                        break;
                    }
                }
                if (is_known)
                {
                    continue;
                }

                ClosedBackPlugin addition;
                addition.plugin      = dependency;
                addition.required_by = plugin.name;
                closed_back.push_back(std::move(addition));
            }
        }

        // 链接边：dependencies 的无条件边 + 生效选项的条件边
        std::vector<LinkEdge> edges;
        std::vector<std::string> common_headers;
        std::vector<std::string> vendor_dirs;
        std::vector<PluginCompileFacts> compile_facts;

        for (const EffectivePlugin& plugin : closure)
        {
            const CatalogEntry* entry = find_plugin(catalog, plugin.name);
            if (entry == nullptr)
            {
                continue;
            }
            const PluginManifest& manifest = entry->manifest;

            EdgeBuilder builder(plugin.name);
            for (const std::string& dependency : manifest.dependencies)
            {
                builder.add_unconditional(dependency);
            }
            for (const ManifestOption& option : manifest.options)
            {
                if (option.links_when_on.empty())
                {
                    continue;
                }
                bool is_on = false;
                if (!find_option_value(plugin, option.name, is_on) || !is_on)
                {
                    continue;   // 生效值为 OFF 的选项不产生边
                }
                for (const std::string& upstream : option.links_when_on)
                {
                    builder.add_conditional(upstream, option.name);
                }
            }
            const std::vector<LinkEdge> plugin_edges = builder.take_edges();
            edges.insert(edges.end(), plugin_edges.begin(), plugin_edges.end());

            for (const std::string& header : manifest.common_headers)
            {
                common_headers.push_back(header);
            }
            for (const SharedVendor& vendor : manifest.shared_vendors)
            {
                if (vendor.target.has_value())
                {
                    vendor_dirs.push_back(vendor.dir);
                }
            }
            compile_facts.push_back(make_compile_facts(manifest));
        }

        sort_edges(edges);
        sort_names(common_headers);
        common_headers.erase(std::unique(common_headers.begin(), common_headers.end()), common_headers.end());
        sort_names(vendor_dirs);
        vendor_dirs.erase(std::unique(vendor_dirs.begin(), vendor_dirs.end()), vendor_dirs.end());

        std::vector<std::string> nodes;
        nodes.reserve(closure.size());
        for (const EffectivePlugin& plugin : closure)
        {
            nodes.push_back(plugin.name);
        }
        sort_names(nodes);

        const TopoResult topo = topological_order(nodes, edges);
        if (!topo.is_ok)
        {
            std::string cycle_text;
            for (std::size_t index = 0; index < topo.cycle.size(); ++index)
            {
                if (index != 0)
                {
                    cycle_text += " → ";
                }
                cycle_text += topo.cycle[index];
            }
            if (!topo.cycle.empty())
            {
                cycle_text += " → ";
                cycle_text += topo.cycle.front();
            }

            resolution.diagnostics.add(ProjectDiagnostic{ProjectCategory::DependencyCycle,
                                                         std::string("dependencies"),
                                                         std::string("依赖图无环"),
                                                         "环：" + cycle_text,
                                                         std::string("plugin_config.json"),
                                                         std::string("由插件作者删除环上的某条 dependencies 后重跑")});
        }

        resolution.edges          = std::move(edges);
        resolution.init_order     = topo.order;
        resolution.common_headers = std::move(common_headers);
        resolution.vendor_dirs    = std::move(vendor_dirs);
        resolution.compile_facts  = std::move(compile_facts);
        resolution.closed_back    = std::move(closed_back);
        resolution.selection.direct = selection.direct;
        resolution.selection.all    = std::move(closure);
        resolution.is_valid         = !resolution.diagnostics.has_error() && topo.is_ok;
        return resolution;
    }
} // namespace ID::Project
