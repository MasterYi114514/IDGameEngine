#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "project/Base/PathText.hpp"

/**
 *  记录文件与受管目录的固定相对路径，以及 payload / backup 的相对路径构造。
 *
 *  只提供常量与纯文本拼接：日志文件名与序号由日志单元的 open_log 在项目锁内分配，本头不参与。
 *
 *  不变量：
 *  1. 常量一律是相对项目根的 POSIX 路径，记录里出现的就是这些相对形态。
 *  2. `payload_relative` / `backup_relative` 是这两个路径形态的唯一构造入口，调用方不手写拼接。
 *  3. 普通文件 → `<前缀>/<attempt 或 generation>/<path>`；受控片段 →
 *     `<前缀>/<attempt 或 generation>/fragments/<fragment>.txt`。
 */

namespace ID::Project
{
    inline constexpr std::string_view kResolvedRelative = "IDGenerated/resolved.json";
    inline constexpr std::string_view kManifestRelative = "IDGenerated/manifest.json";
    inline constexpr std::string_view kIntentRelative   = "IDGenerated/intent.json";
    inline constexpr std::string_view kBuildRelative    = "IDGenerated/build.json";
    inline constexpr std::string_view kLockRelative     = "IDGenerated/.lock";
    inline constexpr std::string_view kLogDirectory     = "IDGenerated/log";
    inline constexpr std::string_view kBackupDirectory  = "IDGenerated/backup";
    inline constexpr std::string_view kBeforeDirectory  = "IDGenerated/before";

    inline constexpr std::string_view kSchemaVersion = "1";
    inline constexpr std::string_view kPluginRoot    = "IDPlugin";
    inline constexpr std::string_view kCommonRoot    = "IDPlugin/Common";
    inline constexpr std::string_view kVendorRoot    = "IDPlugin/Vendor";
    inline constexpr std::string_view kRootCmake     = "CMakeLists.txt";
    inline constexpr std::string_view kSeedRelative  = "src/main.cpp";

    /**
     *  @brief 记录文件的绝对路径
     *  @param project_root 项目根绝对路径
     *  @param relative 记录文件相对路径（本头的 k*Relative 常量）
     */
    inline std::filesystem::path record_path(const std::filesystem::path& project_root, std::string_view relative)
    {
        return project_root / to_native(relative);
    }

    /**
     *  @brief before-payload 的相对路径
     *  @param attempt attempt 标识
     *  @param path 目标相对路径（片段时为其所属文件）
     *  @param fragment 片段名；为空表示整文件条目
     *  @return 普通文件 `<before>/<attempt>/<path>`；片段 `<before>/<attempt>/fragments/<fragment>.txt`
     */
    inline std::string payload_relative(std::string_view attempt, std::string_view path,
                                        std::optional<std::string_view> fragment)
    {
        std::string result(kBeforeDirectory);
        result += '/';
        result += attempt;
        if (fragment.has_value())
        {
            result += "/fragments/";
            result += *fragment;
            result += ".txt";
            return result;
        }
        result += '/';
        result += path;
        return result;
    }

    /**
     *  @brief `--force` 备份的相对路径
     *  @param generation generation 标识
     *  @param path 目标相对路径（片段时为其所属文件）
     *  @param fragment 片段名；为空表示整文件条目
     *  @return 普通文件 `<backup>/<generation>/<path>`；片段 `<backup>/<generation>/fragments/<fragment>.txt`
     */
    inline std::string backup_relative(std::string_view generation, std::string_view path,
                                       std::optional<std::string_view> fragment)
    {
        std::string result(kBackupDirectory);
        result += '/';
        result += generation;
        if (fragment.has_value())
        {
            result += "/fragments/";
            result += *fragment;
            result += ".txt";
            return result;
        }
        result += '/';
        result += path;
        return result;
    }
} // namespace ID::Project
