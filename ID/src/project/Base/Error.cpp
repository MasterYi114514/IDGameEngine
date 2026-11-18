#include "project/Base/Error.hpp"

/**
 *  ProjectCategory 的标识符、人读说明与退出码映射实现。
 *
 *  三张表必须同改：新增类别时同步改 spec §13.2、Error.hpp 的枚举与 static_assert、本文件的
 *  to_identifier / to_text 分支、exit_mapping 的分组，只改一处会造成"标识与映射各说一套"。
 */

namespace ID::Project
{
    namespace
    {
        // spec §13.1 的全序：数组下标即优先级序号，数值越小越优先（最后一项是成功码）
        inline constexpr int kPriorityOrder[] =
        {
            kExitUsage,             // 0：用法错误
            kExitValidation,        // 1：输入或一致性
            kExitOwnership,         // 2：写入、所有权、记录、恢复、并发
            kExitCleanupBlocked,    // 3：清理阻断
            kExitBuild,             // 4：构建
            kExitSuccess,           // 5：成功（类别不产生该码，保留以对齐全序）
        };

        inline constexpr std::size_t kPriorityCount = sizeof(kPriorityOrder) / sizeof(kPriorityOrder[0]);
    } // namespace

    std::string_view to_identifier(ProjectCategory category) noexcept
    {
        switch (category)
        {
            case ProjectCategory::CliUsage:                     return "cli_usage";
            case ProjectCategory::SelectionSourceConflict:      return "selection_source_conflict";
            case ProjectCategory::RegistryInvalid:              return "registry_invalid";
            case ProjectCategory::ProjectExists:                return "project_exists";
            case ProjectCategory::ProjectNotFound:              return "project_not_found";
            case ProjectCategory::ProjectNameInvalid:           return "project_name_invalid";
            case ProjectCategory::ProjectPathInvalid:           return "project_path_invalid";
            case ProjectCategory::ProjectPathOverlap:           return "project_path_overlap";
            case ProjectCategory::ProjectNotInitialized:        return "project_not_initialized";
            case ProjectCategory::ProjectConfigMissing:         return "project_config_missing";
            case ProjectCategory::ProjectConfigInvalid:         return "project_config_invalid";
            case ProjectCategory::ProjectNameMismatch:          return "project_name_mismatch";
            case ProjectCategory::PluginDuplicate:              return "plugin_duplicate";
            case ProjectCategory::PluginUnknown:                return "plugin_unknown";
            case ProjectCategory::DependencyCycle:              return "dependency_cycle";
            case ProjectCategory::RequirementConflict:          return "requirement_conflict";
            case ProjectCategory::OptionUnknown:                return "option_unknown";
            case ProjectCategory::OptionValueInvalid:           return "option_value_invalid";
            case ProjectCategory::AssetMissing:                 return "asset_missing";
            case ProjectCategory::PluginAssetIncomplete:        return "plugin_asset_incomplete";
            case ProjectCategory::PathOutsideProject:           return "path_outside_project";
            case ProjectCategory::CmakeNameConflict:            return "cmake_name_conflict";
            case ProjectCategory::StaleGenerated:               return "stale_generated";
            case ProjectCategory::WriterConflict:               return "writer_conflict";
            case ProjectCategory::UnmanagedContent:             return "unmanaged_content";
            case ProjectCategory::ChangedSincePlan:             return "changed_since_plan";
            case ProjectCategory::WriteFailed:                  return "write_failed";
            case ProjectCategory::RecoveryConflict:             return "recovery_conflict";
            case ProjectCategory::RecordInvalid:                return "record_invalid";
            case ProjectCategory::SnapshotUnstable:             return "snapshot_unstable";
            case ProjectCategory::BuildEnvironmentUnsupported:  return "build_environment_unsupported";
            case ProjectCategory::BuildInvokeFailed:            return "build_invoke_failed";
            case ProjectCategory::BuildFailed:                  return "build_failed";
            case ProjectCategory::ArtifactMissing:              return "artifact_missing";
            case ProjectCategory::CleanupBlocked:               return "cleanup_blocked";
            case ProjectCategory::RegistryMarkerStale:          return "registry_marker_stale";
            case ProjectCategory::PluginConfigInvalid:          return "plugin_config_invalid";
            case ProjectCategory::DependencyUnknown:            return "dependency_unknown";
        }

        // 38 个类别全部在上表列出；只有用 static_cast 造出域外值时才到达这里。
        // 不把未知取值静默映射成某个具体类别，以免诊断指向错误对象
        return "unknown_category";
    }

    std::string_view to_text(ProjectCategory category) noexcept
    {
        switch (category)
        {
            case ProjectCategory::CliUsage:
                return "命令行参数不合法";
            case ProjectCategory::SelectionSourceConflict:
                return "插件选择来源冲突";
            case ProjectCategory::RegistryInvalid:
                return "本机项目登记无法读取或结构非法";
            case ProjectCategory::ProjectExists:
                return "项目名或路径已被占用";
            case ProjectCategory::ProjectNotFound:
                return "项目未登记";
            case ProjectCategory::ProjectNameInvalid:
                return "项目名不满足命名规则";
            case ProjectCategory::ProjectPathInvalid:
                return "项目路径不合法";
            case ProjectCategory::ProjectPathOverlap:
                return "项目路径与工程根重叠";
            case ProjectCategory::ProjectNotInitialized:
                return "项目没有已提交的组装";
            case ProjectCategory::ProjectConfigMissing:
                return "项目缺少选择文件 plugins.json";
            case ProjectCategory::ProjectConfigInvalid:
                return "项目选择文件无法解析或字段非法";
            case ProjectCategory::ProjectNameMismatch:
                return "选择文件中的项目名与登记不一致";
            case ProjectCategory::PluginDuplicate:
                return "同一插件被重复选择";
            case ProjectCategory::PluginUnknown:
                return "引用了未发现的插件";
            case ProjectCategory::DependencyCycle:
                return "依赖闭包成环";
            case ProjectCategory::RequirementConflict:
                return "链接方式与插件要求冲突";
            case ProjectCategory::OptionUnknown:
                return "使用了未声明的选项";
            case ProjectCategory::OptionValueInvalid:
                return "选项取值不在合法集内";
            case ProjectCategory::AssetMissing:
                return "清单声明的资产在源插件中缺失";
            case ProjectCategory::PluginAssetIncomplete:
                return "插件必备交付件缺失";
            case ProjectCategory::PathOutsideProject:
                return "条目路径越出项目根或形态非法";
            case ProjectCategory::CmakeNameConflict:
                return "生成的 CMake 名字重复或非法";
            case ProjectCategory::StaleGenerated:
                return "已提交的生成结果与当前选择不一致";
            case ProjectCategory::WriterConflict:
                return "同一目标已有活跃写入者";
            case ProjectCategory::UnmanagedContent:
                return "目标内容不受本工具管理（本地修改或未登记路径）";
            case ProjectCategory::ChangedSincePlan:
                return "磁盘状态在计划之后发生变化";
            case ProjectCategory::WriteFailed:
                return "写入、替换或记录落盘失败";
            case ProjectCategory::RecoveryConflict:
                return "未完成事务与当前状态冲突";
            case ProjectCategory::RecordInvalid:
                return "记录缺失、损坏或互不一致";
            case ProjectCategory::SnapshotUnstable:
                return "只读快照期间目标被修改";
            case ProjectCategory::BuildEnvironmentUnsupported:
                return "请求的构建身份不受支持";
            case ProjectCategory::BuildInvokeFailed:
                return "构建进程无法启动";
            case ProjectCategory::BuildFailed:
                return "构建返回非零";
            case ProjectCategory::ArtifactMissing:
                return "构建成功但产物不成立";
            case ProjectCategory::CleanupBlocked:
                return "清理范围内存在不可删除内容";
            case ProjectCategory::RegistryMarkerStale:
                return "登记的初始化标记待收尾（warn）";
            case ProjectCategory::PluginConfigInvalid:
                return "插件清单非法，该插件被忽略（warn）";
            case ProjectCategory::DependencyUnknown:
                return "插件依赖引用了未知插件，该插件被忽略（warn）";
        }

        return "未知类别";
    }

    ExitMapping exit_mapping(ProjectCategory category) noexcept
    {
        switch (category)
        {
            // 退出码 3：用法
            case ProjectCategory::CliUsage:
            case ProjectCategory::SelectionSourceConflict:
                return ExitMapping{kExitUsage, true};

            // 退出码 1：输入或一致性
            case ProjectCategory::RegistryInvalid:
            case ProjectCategory::ProjectExists:
            case ProjectCategory::ProjectNotFound:
            case ProjectCategory::ProjectNameInvalid:
            case ProjectCategory::ProjectPathInvalid:
            case ProjectCategory::ProjectPathOverlap:
            case ProjectCategory::ProjectNotInitialized:
            case ProjectCategory::ProjectConfigMissing:
            case ProjectCategory::ProjectConfigInvalid:
            case ProjectCategory::ProjectNameMismatch:
            case ProjectCategory::PluginDuplicate:
            case ProjectCategory::PluginUnknown:
            case ProjectCategory::DependencyCycle:
            case ProjectCategory::RequirementConflict:
            case ProjectCategory::OptionUnknown:
            case ProjectCategory::OptionValueInvalid:
            case ProjectCategory::AssetMissing:
            case ProjectCategory::PluginAssetIncomplete:
            case ProjectCategory::PathOutsideProject:
            case ProjectCategory::CmakeNameConflict:
            case ProjectCategory::StaleGenerated:
                return ExitMapping{kExitValidation, true};

            // 退出码 2：写入、所有权、记录、恢复、并发
            case ProjectCategory::WriterConflict:
            case ProjectCategory::UnmanagedContent:
            case ProjectCategory::ChangedSincePlan:
            case ProjectCategory::WriteFailed:
            case ProjectCategory::RecoveryConflict:
            case ProjectCategory::RecordInvalid:
            case ProjectCategory::SnapshotUnstable:
                return ExitMapping{kExitOwnership, true};

            // 退出码 4：构建
            case ProjectCategory::BuildEnvironmentUnsupported:
            case ProjectCategory::BuildInvokeFailed:
            case ProjectCategory::BuildFailed:
            case ProjectCategory::ArtifactMissing:
                return ExitMapping{kExitBuild, true};

            // 退出码 5：清理阻断
            case ProjectCategory::CleanupBlocked:
                return ExitMapping{kExitCleanupBlocked, true};

            // 无退出映射：warn 类别不参与退出码选择（spec §13.2）
            case ProjectCategory::RegistryMarkerStale:
            case ProjectCategory::PluginConfigInvalid:
            case ProjectCategory::DependencyUnknown:
                return ExitMapping{kExitSuccess, false};
        }

        // 域外取值按 warn 处理：不改变退出码，也不假装成某个具体类别的码
        return ExitMapping{kExitSuccess, false};
    }

    std::uint8_t exit_priority(ProjectCategory category) noexcept
    {
        const ExitMapping mapping = exit_mapping(category);
        if (!mapping.is_mapped)
        {
            return kNoPriority;
        }

        for (std::size_t index = 0; index < kPriorityCount; ++index)
        {
            if (kPriorityOrder[index] == mapping.code)
            {
                return static_cast<std::uint8_t>(index);
            }
        }

        return kNoPriority;
    }
} // namespace ID::Project
