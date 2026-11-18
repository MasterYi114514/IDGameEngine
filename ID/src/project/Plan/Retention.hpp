#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Plan/Ownership.hpp"
#include "project/Record/ManifestRecord.hpp"
#include "project/Record/ResolvedRecord.hpp"
#include "project/Resolve/AssetPlan.hpp"
#include "project/Resolve/Resolution.hpp"

/**
 *  保留集、删除集与裁剪冲突。
 *
 *  保留集是"本次组装允许存在于项目内的工具可覆盖路径集合"（spec §8.6 的唯一口径）：只由清单声明与
 *  固定条目决定，**不扫描目录猜保留**。
 *
 *  不变量：
 *  1. 保留集的来源按命令分开：`new`/`update` 用 `build_retention`（按清单重算），
 *     `clean` 与 `clean --check` 只用 `retention_from_record`（取已提交 `resolved` 的字段），
 *     因此 `clean` 不读 `plugins.json`、不读清单、不访问源插件库。
 *  2. `retention_from_record` 只做载体转换：记录里的 `retention` 是路径级集合，因此只填路径级条目；
 *     片段身份由 `manifest` 承担，`plan_trim` 只需要路径级包含判定。
 *  3. 删除只覆盖 `IDPlugin` 子树内、归属可证、保留集外的条目；`seed` 条目不参与删除判定。
 *  4. `IDPlugin` 子树内出现未登记路径即整阶段阻断：零删除、零提交，`--force` 不放行（spec §8.8）。
 *  5. 本单元只读磁盘（目标摘要），不写盘、不取锁。
 */

namespace ID::Project
{
    /**
     *  保留集：路径级集合 + 条目级集合。
     *  普通文件只入路径集合；受控片段同时入路径集合（所属文件一次）与条目集合（路径 + 片段名）。
     */
    class RetentionSet
    {
    public:
        /**
         *  @brief 路径是否在保留集内
         *  @param relative 已规范化的相对项目根路径
         */
        bool contains_path(std::string_view relative) const noexcept;

        /**
         *  @brief 条目是否在保留集内
         *  @param relative 已规范化的相对项目根路径
         *  @param fragment 片段名；为空表示整文件条目
         *  @return 路径在集合内且（片段为空、或该片段被显式登记）时返回 true
         */
        bool contains_entry(std::string_view relative, std::optional<std::string_view> fragment) const noexcept;

        /**
         *  @brief 排序去重后的路径集合
         */
        const std::vector<std::string>& paths() const noexcept;

        /**
         *  @brief 追加一个路径级条目（重复追加无副作用）
         */
        void add_path(std::string relative);

        /**
         *  @brief 追加一个条目级条目，同时把所属文件路径加入路径集合
         */
        void add_entry(std::string relative, std::optional<std::string> fragment);

    private:
        std::vector<std::string>                         m_paths   = {};   // 路径级集合，按字节序去重
        std::vector<std::pair<std::string, std::string>> m_entries = {};   // (path, fragment)，按两者字节序去重
    };

    /**
     *  @brief 由资产计划与固定条目构造保留集
     *  @param assets 资产计划（复制集合、生成集合与目录集合）
     *  @param resolution 组装结果（`common_headers` 并集）
     *  @return 保留集
     *  @note 条目来源（spec §8.6）：1 `IDPlugin/CMakeLists.txt`；2–3 每个插件的 `CMakeLists.txt` /
     *        `plugin_config.json` / `README.md` 与 `header_dirs` 命中项；4 `common_headers` 并集；
     *        5–8 `source_dirs` / `vendor_dirs` / `shared_vendors[].dir` / `files`；9 根 `CMakeLists.txt`
     *        的两个受控片段（路径 + 片段名各一条）；10 `src/main.cpp`（seed）；另加
     *        `AssetPlan::directories` 的全部目录
     *  @note 只服务 `new`/`update`：`clean` 不调用本函数
     */
    RetentionSet build_retention(const AssetPlan& assets, const Resolution& resolution);

    /**
     *  @brief 由已提交 `resolved` 的保留集字段构造保留集（`clean` 的唯一来源）
     *  @param record 已提交记录；`retention` 字段就是上次 `new`/`update` 算出的路径集合
     *  @return 只填路径级条目的保留集
     *  @note 不读清单、不读 `plugins.json`、不访问源插件库：源插件目录不可用或用户选择已变时 `clean`
     *        仍可执行（spec §9.3 链路 4）
     */
    RetentionSet retention_from_record(const ResolvedRecord& record);

    /**
     *  一个待删项。
     */
    struct DeletionCandidate
    {
        ManifestEntry    entry        = {};                            // 旧 manifest 条目
        OwnershipVerdict verdict      = OwnershipVerdict::DeleteAllowed;
        std::string      disk_digest  = {};                            // D：删除前磁盘上的摘要（payload 与删除前复验的对象）
        bool             needs_backup = false;                         // --force 覆盖前必须先备份
    };

    /**
     *  裁剪计划：待删项、被阻断项、未登记路径与诊断。
     */
    struct TrimPlan
    {
        std::vector<DeletionCandidate> candidates        = {};   // 待删项（含需要备份的）
        std::vector<ManifestEntry>     blocked_modified  = {};   // 摘要不符且无 --force
        std::vector<std::string>       unregistered      = {};   // 待删范围内的未登记路径
        DiagnosticSet                  diagnostics       = {};
        bool                           is_blocked        = false;  // 任一未登记路径 → 整阶段阻断
        bool                           is_valid          = true;
    };

    /**
     *  @brief 计算删除集合与裁剪冲突
     *  @param old_manifest 可信的旧 manifest；不可信时调用方不得调用本函数
     *  @param retention 本次保留集
     *  @param project_root 项目根绝对路径
     *  @param has_force 是否给了 --force
     *  @return 裁剪计划
     *  @note 规则：只考虑 `entry.path` 落在 `IDPlugin` 子树之内的条目；保留集命中的条目保留；未命中的成为
     *        候选；`seed` 条目不参与删除判定；每个候选按 `judge_ownership`（N = absent）判定，
     *        `DeleteNeedsForce` 计入 `blocked_modified` 并产 `cleanup_blocked`（5）
     *  @note 扫描总是进行（与是否有待删候选无关）：`IDPlugin` 子树内不在旧 manifest、不在保留集内的文件即
     *        未登记路径，`is_blocked` 置真；`--force` 不放行。冲突类别由调用方按命令选定
     *        （写入型命令 `unmanaged_content`、`clean` 为 `cleanup_blocked`），本单元只给集合
     *  @note 条目路径与扫描根的祖先链上任一段命中重解析点即按 `path_outside_project`（1）终止该条判定
     *        （`is_valid` 置假）：链接目录可能指向项目外，既不删除也不跟随枚举（spec §2.3）
     *  @note 只读磁盘摘要，不删除、不写盘
     */
    TrimPlan plan_trim(const ManifestRecord& old_manifest, const RetentionSet& retention,
                       const std::filesystem::path& project_root, bool has_force);
} // namespace ID::Project
