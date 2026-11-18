#include "project/Plan/Snapshot.hpp"

#include <cstddef>
#include <utility>

#include "project/Base/Digest.hpp"
#include "project/Base/PathText.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  只读快照的取样与复验（不变量与受检目标口径见 `Snapshot.hpp`）。
 *
 *  `--check` 的受检目标是该命令的状态文件：登记（`registry_snapshot_target`）加项目内的选择、两份记录、
 *  未完成事务、根构建描述，`build` 另含 `build.json`（`check_snapshot_targets`）。取样分两段——登记文件
 *  在 `load_registry` 之前、项目内文件在它们被读取之前——复验点在计划与逐项结论产出之后，两者之间的
 *  任何改写都让"本次判定看到的状态"失效。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSelectionFile       = "plugins.json";
        constexpr std::string_view kRegistryDescription = " 登记文件 ";
    } // namespace

    std::string snapshot_digest(const FileRead& read)
    {
        return read.status == ReadStatus::Found ? digest_text(read.bytes).to_hex() : std::string();
    }

    std::vector<std::string> sample_snapshot(std::span<const SnapshotTarget> targets)
    {
        std::vector<std::string> digests;
        digests.reserve(targets.size());
        for (const SnapshotTarget& target : targets)
        {
            digests.push_back(snapshot_digest(read_file(target.path)));
        }
        return digests;
    }

    void append_snapshot(std::vector<SnapshotTarget>& targets, std::vector<std::string>& before,
                         std::span<const SnapshotTarget> extra)
    {
        std::vector<std::string> digests = sample_snapshot(extra);
        targets.insert(targets.end(), extra.begin(), extra.end());
        before.insert(before.end(), digests.begin(), digests.end());
    }

    bool compare_snapshot(DiagnosticSet& diagnostics, std::span<const SnapshotTarget> targets,
                          std::span<const std::string> before, std::string_view command_text)
    {
        bool is_stable = true;

        const std::size_t count = targets.size() < before.size() ? targets.size() : before.size();
        for (std::size_t index = 0; index < count; ++index)
        {
            const SnapshotTarget& target = targets[index];
            if (snapshot_digest(read_file(target.path)) == before[index])
            {
                continue;
            }

            is_stable = false;

            std::string next_action = "确认没有其它进程在写";
            next_action += target.description;
            next_action += "后重跑 ";
            next_action += command_text;

            std::string expected = "本次只读扫描期间";
            expected += target.description;
            expected += "未被改写";

            diagnostics.add(ProjectDiagnostic{ProjectCategory::SnapshotUnstable, target.subject,
                                              std::move(expected), "读取前后摘要不同", target.subject,
                                              std::move(next_action)});
        }
        return is_stable;
    }

    SnapshotTarget registry_snapshot_target(const std::filesystem::path& registry_path)
    {
        return SnapshotTarget{registry_path, to_utf8(registry_path), std::string(kRegistryDescription)};
    }

    std::vector<SnapshotTarget> check_snapshot_targets(const std::filesystem::path& project_root,
                                                       CommandKind command)
    {
        std::vector<SnapshotTarget> targets;
        if (command == CommandKind::List)
        {
            // list 只扫登记文件，受检目标由编排层就地构造，不经过本表
            return targets;
        }

        // 项目内文件用相对路径做对象与定位；措辞里前后各留一个空格，与相邻中文分开
        const auto add_relative = [&targets, &project_root](std::string_view relative)
        {
            const std::string text(relative);
            targets.push_back(SnapshotTarget{record_path(project_root, relative), text, " " + text + " "});
        };

        if (command != CommandKind::Clean)
        {
            add_relative(kSelectionFile);
        }
        add_relative(kResolvedRelative);
        add_relative(kManifestRelative);
        // 未完成事务的判定对象：P0 对五条命令都读它（spec §5.8）
        add_relative(kIntentRelative);

        // 生成计划的三个命令由计划选项读根构建描述的存在性；build 与 recover 的 --check 不生成计划
        if (command == CommandKind::New || command == CommandKind::Update || command == CommandKind::Clean)
        {
            add_relative(kRootCmake);
        }

        // build 的复验步骤读既有构建记录（同 build_dir 的生成器一致性），`build --check` 因此也要复验它
        if (command == CommandKind::Build)
        {
            add_relative(kBuildRelative);
        }

        return targets;
    }
} // namespace ID::Project
