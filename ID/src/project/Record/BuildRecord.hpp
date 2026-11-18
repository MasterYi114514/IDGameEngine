#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 *  `build.json`：构建记录（spec §12.4）。顶层数组、逻辑上只追加，不被 `clean` 删除。
 *
 *  本头是载体：判定、写盘与收敛属于构建执行阶段。
 *
 *  不变量：
 *  1. `verdict`、`configure_exit`、`build_exit` 三者独立：某阶段没启动时退出码为空，`verdict` 写明
 *     `invoke_failed` / `not_run`，不写 0。
 *  2. `stage` 只有 `configure` / `build` / `null` 三态，与 `verdict` 不互相推导；与 `manifest` 条目的
 *     `stage`（`copy` / `generate`）同名不同义。
 *  3. `identity` 取不到的字段写 `is_present = false` + `reason`，不伪造。
 *  4. `requested` 与 `actual` 分开记；两侧不一致时由构建阶段判 `identity_mismatch`。
 *  5. `build.json` 是唯一允许出现本机绝对路径的记录（`work_dir`、`build_dir`、`artifacts`、`log_path`）。
 */

namespace ID::Project
{
    /**
     *  一个构建身份字段：是否取到、取值与取不到的原因。
     */
    struct BuildIdentityField
    {
        bool        is_present = false;   // 字段是否取到
        std::string value      = {};      // 取不到时为空，并写 reason
        std::string reason     = {};      // 取不到的原因（如"查询响应缺该字段"）
    };

    /**
     *  一个构建产物。
     */
    struct BuildArtifact
    {
        std::string   path       = {};   // 绝对路径
        std::string   digest     = {};
        std::string   mtime      = {};   // ISO-8601 带偏移；只作信息字段
        std::uint64_t size_bytes = 0;
    };

    /**
     *  一次构建调用的记录。
     */
    struct BuildRecord
    {
        std::string   id                = {};   // generation + 配置 + 序号
        std::string   generation_id     = {};
        std::string   manifest_digest   = {};
        std::string   config            = {};   // "debug" / "release"
        std::string   generator         = {};
        std::string   requested_toolchain = {};
        std::string   requested_arch      = {};
        std::string   actual_toolchain    = {};   // 实测；取不到为空
        std::string   actual_arch         = {};

        BuildIdentityField compiler_id       = {};
        BuildIdentityField compiler_version  = {};
        BuildIdentityField compiler_path     = {};
        BuildIdentityField target_triple     = {};
        BuildIdentityField crt               = {};
        BuildIdentityField ipo               = {};
        BuildIdentityField pointer_width     = {};

        std::vector<std::string>   configure_argv = {};
        std::vector<std::string>   build_argv     = {};
        std::string                work_dir       = {};
        std::string                build_dir      = {};
        std::string                started_at     = {};
        std::string                finished_at    = {};
        std::optional<int>         configure_exit = {};   // 未执行时为空
        std::optional<int>         build_exit     = {};
        std::optional<std::string> stage          = {};   // "configure" / "build"；为空即 null
        std::string                verdict        = {};
        std::vector<BuildArtifact> artifacts      = {};
        std::optional<std::string> log_path       = {};   // 绝对路径；日志打不开时为空
    };

    /**
     *  `build.json` 的顶层内容：数组顺序即发生顺序。
     */
    struct BuildLog
    {
        std::vector<BuildRecord> records = {};
    };
} // namespace ID::Project
