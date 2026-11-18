#include "project/Apply/PayloadStore.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  payload 的写入、回读与按 attempt 收尾。
 *
 *  写入前先校验源内容摘要等于记录的 `before`（不符即 `changed_since_plan`），写入后回读再校验一次
 *  （不符即 `write_failed`）：前者防"计划之后被改"，后者防"payload 自己没落对"。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::size_t kMaxDiscardDepth = 32;

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        // payload 路径必须落在 IDGenerated/before 子树之内：拒绝任何越界拼法
        bool is_payload_path(std::string_view relative)
        {
            return normalize_relative(relative).verdict == PathVerdict::Ok && is_under(relative, kBeforeDirectory);
        }
    } // namespace

    PayloadStore::PayloadStore(const std::filesystem::path& project_root)
        : m_project_root(project_root)
    {
    }

    PayloadStore::Result PayloadStore::save_file(std::string_view payload_relative,
                                                const std::filesystem::path& source_absolute,
                                                const DigestState& expected_before)
    {
        Result result;

        const FileRead source = read_file(source_absolute);
        if (source.status == ReadStatus::Failed)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::ChangedSincePlan, to_utf8(source_absolute),
                                                "能读取该项的当前内容", "读取失败", to_utf8(source_absolute),
                                                "确认该文件未被其它进程占用或有读权限后重跑原命令");
            return result;
        }

        if (expected_before.is_absent)
        {
            if (source.status != ReadStatus::NotFound)
            {
                result.diagnostic = make_diagnostic(ProjectCategory::ChangedSincePlan, to_utf8(source_absolute),
                                                    "该项在计划时不存在", "文件已出现", to_utf8(source_absolute),
                                                    "重跑原命令，让计划按当前磁盘状态重算");
                return result;
            }
            result.is_ok = true;
            return result;
        }

        if (source.status == ReadStatus::NotFound)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::ChangedSincePlan, to_utf8(source_absolute),
                                                "该项在计划时存在", "文件已不存在", to_utf8(source_absolute),
                                                "重跑原命令，让计划按当前磁盘状态重算");
            return result;
        }

        const std::string current_hex = digest_text(source.bytes).to_hex();
        if (current_hex != expected_before.hex)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::ChangedSincePlan, to_utf8(source_absolute),
                                                expected_before.hex, current_hex, to_utf8(source_absolute),
                                                "重跑原命令，让计划按当前磁盘状态重算");
            return result;
        }

        return save_bytes(payload_relative, source.bytes, expected_before);
    }

    PayloadStore::Result PayloadStore::save_fragment(std::string_view payload_relative, std::string_view fragment_bytes,
                                                    const DigestState& expected_before)
    {
        Result result;

        if (expected_before.is_absent)
        {
            // 片段范围此前不存在：没有旧字节要留，只确认调用方确实按 absent 记了该项
            result.is_ok = true;
            return result;
        }

        return save_bytes(payload_relative, fragment_bytes, expected_before);
    }

    PayloadStore::PayloadRead PayloadStore::load(std::string_view payload_relative)
    {
        PayloadRead result;
        if (!is_payload_path(payload_relative))
        {
            return result;
        }

        const FileRead read = read_file(m_project_root / to_native(payload_relative));
        if (read.status == ReadStatus::Failed)
        {
            return result;
        }
        if (read.status == ReadStatus::NotFound)
        {
            result.is_ok = true;
            return result;
        }

        result.is_ok      = true;
        result.is_present = true;
        result.bytes      = read.bytes;
        result.digest     = digest_text(read.bytes);
        return result;
    }

    RemoveOutcome PayloadStore::discard_attempt(std::string_view attempt)
    {
        RemoveOutcome result;

        std::string root_relative(kBeforeDirectory);
        root_relative += '/';
        root_relative += attempt;
        if (!is_payload_path(root_relative))
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(attempt),
                                                "attempt 标识是合法的目录名", "标识含非法路径字符",
                                                std::string(root_relative),
                                                "这是实现缺陷：按 intent.attempt 的原始取值清理");
            return result;
        }

        const std::filesystem::path root_absolute = m_project_root / to_native(root_relative);

        bool is_query_ok = false;
        const bool is_existing = path_exists(root_absolute, is_query_ok);
        if (!is_query_ok)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, to_utf8(root_absolute),
                                                "能查询 payload 目录属性", "属性查询失败", to_utf8(root_absolute),
                                                "确认该目录权限后重跑收尾");
            return result;
        }
        if (!is_existing)
        {
            result.is_ok = true;
            return result;
        }

        // 逐层收集：文件先删、目录自底向上删；只在这个 attempt 目录之内
        std::vector<std::string> directories;
        directories.push_back(root_relative);

        bool is_failed = false;
        for (std::size_t index = 0; index < directories.size() && index <= kMaxDiscardDepth; ++index)
        {
            // 取副本而不是引用：循环里的 `push_back` 会让容器重新分配，引用会立即悬垂
            // （悬垂后拼出的子路径是垃圾，枚举与删除都会"查不到"而静默跳过，收尾就删不净）
            const std::string  relative = directories[index];
            const DirectoryList listing = list_directory(m_project_root / to_native(relative));
            if (!listing.is_ok)
            {
                is_failed = true;
                break;
            }

            for (const std::string& name : listing.names)
            {
                std::string child(relative);
                child += '/';
                child += name;

                bool child_query_ok = false;
                const bool is_child_directory = is_directory(m_project_root / to_native(child), child_query_ok);
                if (!child_query_ok)
                {
                    is_failed = true;
                    break;
                }
                if (is_child_directory)
                {
                    directories.push_back(std::move(child));
                    continue;
                }

                const RemoveOutcome removed = remove_file(m_project_root / to_native(child));
                if (!removed.is_ok)
                {
                    is_failed = true;
                    break;
                }
            }
            if (is_failed)
            {
                break;
            }
        }

        if (is_failed)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, to_utf8(root_absolute),
                                                "能删除该 attempt 的 payload", "目录列举或删除失败",
                                                to_utf8(root_absolute),
                                                "关闭占用该目录的进程后重跑收尾；残留由下一次同一入口清理");
            return result;
        }

        for (auto directory = directories.rbegin(); directory != directories.rend(); ++directory)
        {
            const RemoveOutcome removed = remove_empty_directory(m_project_root / to_native(*directory));
            if (!removed.is_ok && removed.diagnostic.has_value())
            {
                result.diagnostic = removed.diagnostic;
                return result;
            }
        }

        result.is_ok = true;
        return result;
    }

    PayloadStore::Result PayloadStore::save_bytes(std::string_view payload_relative, std::string_view bytes,
                                                 const DigestState& expected_before)
    {
        Result result;

        if (!is_payload_path(payload_relative))
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(payload_relative),
                                                "payload 路径落在 IDGenerated/before 子树之内", "路径越界或形态非法",
                                                std::string(payload_relative),
                                                "这是实现缺陷：payload 路径必须由 RecordPaths 的构造入口生成");
            return result;
        }

        const std::filesystem::path target = m_project_root / to_native(payload_relative);
        const std::string parent = std::string(parent_relative(payload_relative));
        if (!parent.empty())
        {
            const RemoveOutcome created = ID::Project::create_directories(m_project_root / to_native(parent));
            if (!created.is_ok)
            {
                result.diagnostic = created.diagnostic;
                return result;
            }
        }

        WriteRequest request;
        request.target               = target;
        request.bytes                = std::string(bytes);
        request.is_skip_if_identical = false;   // payload 是恢复素材：内容相同也必须确认真的存在
        request.point                = InjectPoint::P3BeforePayload;
        request.subject              = std::string(payload_relative);

        const WriteOutcome outcome = write_file_atomic(request);
        if (!outcome.is_ok)
        {
            result.diagnostic = outcome.diagnostic;
            return result;
        }

        const FileRead read_back = read_file(target);
        if (read_back.status != ReadStatus::Found)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(payload_relative),
                                                "payload 写后能回读", "回读失败", std::string(payload_relative),
                                                "确认该目录可写后重跑原命令");
            return result;
        }

        result.digest = digest_text(read_back.bytes);
        if (result.digest.to_hex() != expected_before.hex)
        {
            result.diagnostic = make_diagnostic(ProjectCategory::WriteFailed, std::string(payload_relative),
                                                expected_before.hex, result.digest.to_hex(),
                                                std::string(payload_relative),
                                                "确认磁盘与杀毒软件未干扰写入后重跑原命令");
            return result;
        }

        result.is_ok      = true;
        result.is_written = true;
        return result;
    }
} // namespace ID::Project
