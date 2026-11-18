#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/Lock.hpp"
#include "project/Record/IntentRecord.hpp"

/**
 *  一次写入会话：持有项目锁，并在需要时短时嵌套登记锁。
 *
 *  取锁顺序固定：项目锁 → 登记锁；登记锁只在项目锁内短时取得、立即释放（spec §2.5）。本类不提供
 *  "先取登记锁"的入口，因此不可能写出反向等待。
 *
 *  不变量：
 *  1. `open` 在取到项目锁之后**一次**读取 `resolved.json` / `manifest.json` 的字节：同一份字节既产出
 *     `base_digests()`，也产出随后写进 `intent` 的 base 完整内容。摘要与内容同源，不为后者再读一次盘。
 *  2. 基线只服务 `execute_plan` 的锁内复验（spec §2.5），不参与任何冲突或可信性判定。
 *  3. bootstrap 只创建 `IDGenerated/` 与锁文件；创建失败清理本次新建的空目录（`references/memory.md` §7）。
 *  4. 任何退出路径都释放锁、删除自己持有的锁文件；析构也释放（`Abort` 注入的进程击杀是例外，
 *     那正是"进程被杀"的语义，由下次运行按陈旧锁回收）。
 *  5. 记录不存在的一侧记 `absent`；读取失败或损坏按 `record_invalid` 报告，不当作不存在。
 */

namespace ID::Project
{
    /**
     *  一次写入会话。
     */
    class Session
    {
    public:
        Session() = default;
        ~Session();

        Session(const Session&)            = delete;
        Session& operator=(const Session&) = delete;

        /**
         *  打开结果：失败时 `diagnostic` 是 `writer_conflict` / `write_failed` / `record_invalid`。
         */
        struct OpenResult
        {
            bool                             is_ok               = false;
            std::optional<ProjectDiagnostic> diagnostic          = {};
            bool                             created_directories = false;   // 是否为 bootstrap 创建了目录
        };

        /**
         *  会话开启时读到的两份已提交记录字节摘要：锁内基线复验的唯一比较基准。
         */
        struct BaseDigests
        {
            std::optional<Digest> resolved_digest = {};   // 文件不存在为 absent
            std::optional<Digest> manifest_digest = {};
        };

        /**
         *  @brief 打开会话：取项目锁，并在锁内记录两份记录摘要
         *  @param project_root 项目根绝对路径
         *  @param is_create_allowed 为真时允许按最小 bootstrap 创建 `IDGenerated/`（spec §6.4 例外）
         *  @return 打开结果；成功后本对象持有项目锁直到 `close()` 或析构
         *  @note 同一对象重复 `open` 属调用方违约：已打开时直接返回成功，不重复取锁
         *  @note 锁内读取失败按 `record_invalid`；创建或取锁失败按 `write_failed` / `writer_conflict`
         */
        OpenResult open(const std::filesystem::path& project_root, bool is_create_allowed);

        /**
         *  @brief 读取会话开启时记录的基线摘要（`open` 成功之前两侧都是 absent）
         */
        const BaseDigests& base_digests() const noexcept;

        /**
         *  @brief 会话开启时读到的 base 完整内容（写进 `intent` 的 base 快照）
         *  @note 与 `base_digests()` 出自同一次锁内读取；文件不存在时 `is_present` 为假
         */
        const RecordSnapshot& base_resolved() const noexcept;

        /**
         *  @brief 会话开启时读到的 manifest 完整内容
         */
        const RecordSnapshot& base_manifest() const noexcept;

        /**
         *  @brief 在项目锁内短时取登记锁、执行回调、立即释放
         *  @param registry_path `<工程根>/ID/project.json`
         *  @param action 回调；参数 `is_locked` 为假表示登记锁没取到，回调不得写盘并按 `write_failed` 处理
         *  @return 回调的返回值原样返回
         *  @note 回调执行期间登记锁被持有；不得在回调内再取登记锁（本类已持有项目锁）
         */
        template <typename ActionType>
        auto with_registry_lock(const std::filesystem::path& registry_path, ActionType&& action)
        {
            ProjectLock registry_lock;
            const std::filesystem::path lock_path = registry_path.parent_path() / "project.lock";
            const ProjectLock::AcquireResult acquired = registry_lock.acquire(lock_path, LockScope::Registry);
            return std::invoke(std::forward<ActionType>(action), acquired.verdict == LockVerdict::Acquired);
        }

        /**
         *  @brief 结束会话：释放项目锁（析构也会释放，这里用于显式收尾）
         */
        void close() noexcept;

        /**
         *  @brief 当前是否持有项目锁
         */
        bool is_open() const noexcept;

        /**
         *  @brief 会话的项目根；未打开时为空路径
         */
        const std::filesystem::path& project_root() const noexcept;

    private:
        std::filesystem::path   m_project_root        = {};
        ProjectLock             m_lock                = {};
        BaseDigests             m_base_digests        = {};
        RecordSnapshot          m_base_resolved       = {};
        RecordSnapshot          m_base_manifest       = {};
        std::vector<std::filesystem::path> m_created_directories = {};   // 本次 bootstrap 新建的目录（逆序清理）
        bool                    m_is_open             = false;
    };

    /**
     *  @brief 生成 attempt 标识：`<generation_id 前 8 位>-<pid>-<序号>`
     *  @param generation_id 目标代标识；不足 8 位时取全部
     *  @return 进程内唯一的 attempt 标识
     *  @note 只用于目录与记录内的局部标识，不参与 generation 身份
     */
    std::string make_attempt_id(std::string_view generation_id);
} // namespace ID::Project
