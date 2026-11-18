#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IDJsonDocument.hpp"
#include "IDJsonParser.hpp"
#include "IDJsonValue.hpp"

/**
 *  `id.project` 子域的测试支撑设施（私有头，只被 `Benchmark/src/id/id_project_*.cpp` 包含）。
 *
 *  职责：把"一次 `ID project ...` 调用 + 一个隔离沙箱 + 可核对的磁盘状态"做成可复用设施，
 *  让各用例只写夹具、调用与断言，不重复目录管理、进程管理、摘要与快照代码。
 *
 *  不变量：
 *  1. 沙箱根 = `<BENCHMARK_TEMP_DIR>/id/project/<case-id>/`；假引擎根是 `<caseRoot>/engine`
 *     （含 `engine/ID/` 与 `engine/IDPlugin/`），项目根是 `<caseRoot>/project`，两者互不祖先
 *     （`docs/id/11-verification-matrix.md` §11.1.1）。项目根不落在工程根内。
 *  2. 每个用例一个 `CaseRoot`，不跨用例共享目录；同一用例重复运行时先删同名目录。
 *  3. 进程内调用走 `ID::Project::run_command_line`（`IDCore` 的公开入口，Runner.hpp 明确供
 *     `Benchmark` 使用）；`Abort` 注入必须走子进程（`Abort` 会 `_Exit` 调用方进程）。
 *  4. 摘要实现是本测试侧独立的 SHA-256（不与被测实现的 `Digest` 共享代码），构造后用 FIPS 已知
 *     向量自检；自检失败时依赖摘要的用例登记 `Uncovered`，不换成被测实现的摘要。
 *  5. 输出只经 `Check` / `Skip` / `Uncovered` / `Warn` / `ReportLine`；本文件不写 stdout，也不写
 *     官方报告。
 */

namespace ID::Bench::IdProject
{
    /// @brief 一次调用的可观察结果（进程内或子进程）
    struct CommandResult
    {
        int         exit_code = 0;
        std::string out_text  = {};
        std::string err_text  = {};
    };

    /// @brief 子进程的额外控制项
    struct SpawnOptions
    {
        std::string   inject                   = {};      // `ID_TEST_INJECT` 取值；空表示不注入
        bool          is_path_overridden       = false;   // 是否覆盖 `PATH`
        std::string   path_value               = {};      // `PATH` 覆盖值
        bool          is_generator_overridden  = false;   // 是否覆盖 `CMAKE_GENERATOR`
        std::string   generator_value          = {};      // `CMAKE_GENERATOR` 覆盖值（可为空串）
        bool          is_stdin_null            = true;    // stdin 重定向到空文件（非交互）
        unsigned long timeout_ms               = 120000;  // 超时先终止再如实记录
    };

    /// @brief 子进程结果
    struct SpawnResult : CommandResult
    {
        bool is_spawned   = false;   // CreateProcess 是否成功
        bool is_timed_out = false;   // 是否超时后被终止
    };

    // ---------------- 会话 ----------------

    /// @brief 会话开始：清理并重建 `<BENCHMARK_TEMP_DIR>/id/project`，做一次摘要自检
    void BeginIdProjectSession() noexcept;

    /// @brief 会话结束：删除本次创建的 `<BENCHMARK_TEMP_DIR>/id/project`（失败归档已在此之前完成）
    void EndIdProjectSession() noexcept;

    /// @brief 独立 SHA-256 的 FIPS 自检是否通过（`""` 与 `"abc"` 两个已知向量）
    bool IsDigestSelfTestOk() noexcept;

    /// @brief 会话根目录（`<BENCHMARK_TEMP_DIR>/id/project`）
    std::string SessionRoot() noexcept;

    // ---------------- 沙箱 ----------------

    /**
     *  单个用例的隔离目录。
     */
    class CaseRoot
    {
    public:
        /**
         *  @brief 建立 `<session>/<case_id>/`（先删同名目录），并建好假引擎根的两个子目录
         *  @param case_id 用例标识（`id.project.<case>` 的 case 段；只用字母数字与连字符）
         */
        explicit CaseRoot(std::string_view case_id) noexcept;

        /// @brief 用例结束删除整棵 `<caseRoot>`；调用过 ArchiveEvidence 后仍按正常清理
        ~CaseRoot() noexcept;

        CaseRoot(const CaseRoot&)            = delete;
        CaseRoot& operator=(const CaseRoot&) = delete;

        /// @brief `<caseRoot>`
        const std::string& dir() const noexcept;

        /// @brief `<caseRoot>/engine`（假引擎根；含 `ID/` 与 `IDPlugin/`）
        const std::string& engine_root() const noexcept;

        /// @brief `<caseRoot>/engine/IDPlugin`（源插件库夹具根）
        const std::string& plugin_root() const noexcept;

        /// @brief `<caseRoot>/project`（项目根）
        const std::string& project_root() const noexcept;

        /// @brief 用例根下的相对路径（POSIX 分隔）
        std::string CasePath(std::string_view relative) const;

        /// @brief 项目根下的相对路径（POSIX 分隔）
        std::string ProjectPath(std::string_view relative) const;

        /// @brief 假引擎根下的相对路径（POSIX 分隔）
        std::string EnginePath(std::string_view relative) const;

        /// @brief 把整棵用例根复制到 `<benchmark>/build/id.project/<label>/`（失败取证用）
        void ArchiveEvidence(std::string_view label) const noexcept;

    private:
        std::string m_dir;
        std::string m_engine;
        std::string m_plugin;
        std::string m_project;
    };

    // ---------------- 调用 ----------------

    /// @brief 进程内调用 `ID project ...`（参数是 `project` 之后的全部参数）
    CommandResult RunProject(const CaseRoot& root, std::span<const std::string> arguments) noexcept;

    /// @brief 进程内调用（初始化列表形式）
    CommandResult RunProject(const CaseRoot& root, std::initializer_list<std::string_view> arguments) noexcept;

    /**
     *  @brief 进程内调用并武装一个注入点（只用于 `Fail`；`Abort` 必须用子进程）
     *  @param inject `ID_TEST_INJECT` 文本，如 `p3.before-first-copy=fail`
     */
    CommandResult RunProjectInjected(const CaseRoot& root, std::span<const std::string> arguments,
                                     std::string_view inject) noexcept;

    /// @brief 进程内调用并武装注入点（初始化列表形式）
    CommandResult RunProjectInjected(const CaseRoot& root, std::initializer_list<std::string_view> arguments,
                                     std::string_view inject) noexcept;

    /// @brief 子进程调用（`Abort` 注入、stdin 重定向、PATH/生成器环境覆盖）
    SpawnResult SpawnProject(const CaseRoot& root, std::string_view tag, std::span<const std::string> arguments,
                             const SpawnOptions& options) noexcept;

    /// @brief 子进程调用（初始化列表形式）
    SpawnResult SpawnProject(const CaseRoot& root, std::string_view tag,
                             std::initializer_list<std::string_view> arguments,
                             const SpawnOptions& options) noexcept;

    // ---------------- 判定辅助 ----------------

    /// @brief 一次调用的人读摘要（退出码 + 两路输出前 600 字节），用于拼 Check 的 detail
    std::string Describe(const CommandResult& result) noexcept;

    /// @brief 断言退出码等于期望值，detail 必带实际两路输出摘要
    bool CheckExit(std::string_view name, const CommandResult& result, int expected,
                   std::string_view expectation) noexcept;

    /// @brief 断言退出码落在期望集合内（如 `--check` 的 0/6）
    bool CheckExitOneOf(std::string_view name, const CommandResult& result, std::initializer_list<int> expected,
                        std::string_view expectation) noexcept;

    /// @brief 子进程是否成功启动、未超时且退出码等于期望值
    bool CheckSpawnExit(std::string_view name, const SpawnResult& result, int expected,
                        std::string_view expectation) noexcept;

    /// @brief 两次输出里是否出现某类别名（诊断的"类别"行给出稳定标识）
    bool HasCategory(const CommandResult& result, std::string_view category) noexcept;

    /// @brief 输出文本里是否逐字出现某个片段
    bool Contains(const CommandResult& result, std::string_view needle) noexcept;

    class CaseVerdict;

    /**
     *  @brief 判一次调用的退出码并写进 verdict；不匹配时把实际退出码与诊断首段附进 detail
     *  @return 退出码匹配返回 true
     */
    bool AddExit(CaseVerdict& verdict, std::string_view label, const CommandResult& result, int expected) noexcept;

    /**
     *  @brief 判退出码落在期望集合内并写进 verdict（用于 spec §13.1 全序允许的多种合法码）
     *  @param expected 允许的退出码集合，按全序来源在 detail 里由调用方在 label 中说明
     *  @return 命中任一取值返回 true
     */
    bool AddExitOneOf(CaseVerdict& verdict, std::string_view label, const CommandResult& result,
                      std::initializer_list<int> expected) noexcept;

    /// @brief 抽取输出里全部 `类别: <标识>`（逗号分隔），用于失败定位
    std::string CategoryList(const CommandResult& result) noexcept;

    /**
     *  @brief 判输出里出现某类别；不匹配时把实际出现的类别列表附进 detail
     *  @return 命中返回 true
     */
    bool CheckCategory(CaseVerdict& verdict, std::string_view label, const CommandResult& result,
                       std::string_view category) noexcept;

    /**
     *  单条用例的判定累积器：一个 `id.project.<case>` 只出一条判定行，子条件逐项记在 detail 里。
     *  这样报告里的用例行数与矩阵条目数一一对应，又不丢失败定位信息。
     */
    class CaseVerdict
    {
    public:
        /// @brief 记一个子条件（`label=ok` / `label=FAIL`）
        void Add(bool is_ok, std::string_view label) noexcept;

        /// @brief 记一个带实测值的子条件（`label=<actual text>`，不判定）
        void Note(std::string_view label, std::string_view actual) noexcept;

        /// @brief 全部子条件是否成立
        bool IsOk() const noexcept;

        /// @brief detail 文本（子条件按记录顺序拼接）
        const std::string& Detail() const noexcept;

    private:
        bool        m_is_ok = true;
        std::string m_detail = {};
    };

    // ---------------- 文件与目录 ----------------

    bool WriteText(const std::string& path, std::string_view text) noexcept;
    bool ReadText(const std::string& path, std::string& out) noexcept;
    bool Exists(const std::string& path) noexcept;
    bool IsRegularFile(const std::string& path) noexcept;
    bool IsDirectory(const std::string& path) noexcept;
    bool RemoveTree(const std::string& path) noexcept;
    bool CreateDirectories(const std::string& path) noexcept;
    bool CopyTree(const std::string& from, const std::string& to) noexcept;

    /**
     *  @brief 用 `cmd /c mklink /J` 建目录 junction
     *  @return 建成功（链接目录可枚举）返回 true；失败时调用方按"环境不可构造"登记 Uncovered
     *  @note 只被 C12 / J2 的 junction 子项使用；失败不重试
     */
    bool CreateJunction(const std::string& link_path, const std::string& target_path) noexcept;

    /// @brief 递归列条目（排序后的 `DIR <rel>` / `FILE <rel>`，POSIX 分隔、不含根）
    std::vector<std::string> ListTree(const std::string& directory) noexcept;

    /// @brief 递归条目数（目录与文件都算）：用于"零写/零文件"类断言
    std::size_t CountTreeEntries(const std::string& directory) noexcept;

    /// @brief 递归普通文件数
    std::size_t CountRegularFiles(const std::string& directory) noexcept;

    // ---------------- 摘要与快照 ----------------

    /// @brief 测试侧独立实现的 SHA-256（小写十六进制；空输入也返回 64 位）
    std::string Sha256Hex(std::string_view bytes) noexcept;

    /// @brief 文件字节的 SHA-256；读失败时 `is_ok` 置假
    std::string Sha256OfFile(const std::string& path, bool& is_ok) noexcept;

    /// @brief 平台原生 mtime 刻度（只比较相等/不等）；取不到时 `is_ok` 置假
    std::string LastWriteTicks(const std::string& path, bool& is_ok) noexcept;

    /**
     *  只读快照（`docs/id/11-verification-matrix.md` §11.2.1）：项目根全树 + 假引擎根 `ID/`。
     *  条目文本为 `DIR <rel>` / `FILE <rel> <sha256> <mtime ticks>`，已排序。
     *  @note 不含 `IDGenerated/.lock`：它是每条命令都会取/放的互斥文件，不属于内容写入；
     *        锁语义由 N1 / F15 单独覆盖，混进快照会让"零写"判定在每条命令上恒失败
     */
    struct Snapshot
    {
        std::vector<std::string> entries = {};
    };

    /// @brief 取快照（项目根 + 假引擎根 `ID/`；不含 `engine/IDPlugin`）
    Snapshot TakeSnapshot(const CaseRoot& root) noexcept;

    /// @brief 两份快照的差异文本；空串表示无差异
    std::string SnapshotDiff(const Snapshot& before, const Snapshot& after) noexcept;

    // ---------------- JSON 读取 ----------------

    /**
     *  只读 JSON 文档：复用 `IDJson` 的解析器（生产记录本身就是 JSON，解析不是被测对象）。
     *  摘要一律用本文件独立的 SHA-256，不复用被测实现的 `Digest`。
     */
    class JsonReader
    {
    public:
        JsonReader() noexcept = default;

        explicit JsonReader(std::string_view text) noexcept;

        /// @brief 重新解析一段文本（先 reset 文档）
        bool LoadText(std::string_view text) noexcept;

        /// @brief 读文件并解析（文件缺失或解析失败都返回 false，不抛异常）
        bool LoadFile(const std::string& path) noexcept;

        /// @brief 解析是否成功
        bool IsOk() const noexcept;

        /// @brief 按点分路径取字符串（`a.b.0.c`）；缺失或类型不符返回空串
        std::string String(std::string_view dotted_path) const noexcept;

        /// @brief 按点分路径取布尔；缺失时返回 `fallback`
        bool Bool(std::string_view dotted_path, bool fallback) const noexcept;

        /// @brief 按点分路径取整数；缺失或类型不符时返回 `fallback`
        long long Int(std::string_view dotted_path, long long fallback) const noexcept;

        /// @brief 按点分路径取数组/对象元素个数；缺失时返回 0
        std::size_t Count(std::string_view dotted_path) const noexcept;

        /// @brief 按点分路径取节点；缺失返回 nullptr
        const ID::Json::JsonValue* Find(std::string_view dotted_path) const noexcept;

        /// @brief 取根值引用
        const ID::Json::JsonValue& Root() const noexcept;

    private:
        ID::Json::JsonDocument m_document;
        bool                   m_is_ok = false;
    };

    // ---------------- 夹具 ----------------

    /**
     *  合成插件夹具：清单十项字段齐备，资产可被真实构建（`src/<name>.cpp` 有一个可链接符号）。
     *  字段文本直接给 JSON 片段，便于构造非法清单（C 组的负例只需要覆盖其中一个片段）。
     */
    struct PluginSpec
    {
        std::string name                 = {};
        std::string dependencies_json    = "[]";
        std::string requirements_json    = "[]";
        std::string header_dirs_json     = "[\"include\"]";
        std::string common_headers_json  = "[]";
        std::string source_dirs_json     = "[\"src\"]";
        std::string vendor_dirs_json     = "[]";
        std::string shared_vendors_json  = "[]";
        std::string files_json           = "[\"LICENSE\"]";
        std::string options_json         = "[]";
        bool        has_include          = true;    // 写 `include/<name>.hpp`
        bool        has_src              = true;    // 写 `src/<name>.cpp`
        bool        has_readme           = true;    // 写 `README.md`
        bool        has_license          = true;    // 写 `LICENSE`
        bool        has_manifest         = true;    // 写 `plugin_config.json`
        std::string manifest_override    = {};      // 非空时整份清单文本用它（构造非法清单）
    };

    /// @brief 在假引擎根的 `IDPlugin/<name>/` 写一个合成插件
    bool WritePlugin(const CaseRoot& root, const PluginSpec& spec) noexcept;

    /// @brief 从真实工程 `IDPlugin/` 复制一个插件目录（同名覆盖），用于真实构建组合
    bool CopyRealPlugin(const CaseRoot& root, std::string_view name) noexcept;

    /// @brief 从真实工程复制 `Common/` 与 `Vendor/` 两棵共享资产树
    bool CopyRealSharedAssets(const CaseRoot& root) noexcept;

    /// @brief 真实工程根（由 `BENCHMARK_ID_EXE` 的所在目录 `bin/` 的父目录解析；空表示不可用）
    std::string RealEngineRoot() noexcept;

    /// @brief 写 `plugins.json`（直接给 `plugins` 数组文本）
    bool WritePluginsJson(const CaseRoot& root, std::string_view project_name, std::string_view plugins_json) noexcept;

    /// @brief 读记录文件（`IDGenerated/<file>`）的文本；缺失返回 false
    bool ReadProjectRecord(const CaseRoot& root, std::string_view file, std::string& out) noexcept;

    /// @brief 读 `IDGenerated/resolved.json`
    bool ReadResolved(const CaseRoot& root, JsonReader& reader) noexcept;

    /// @brief 读 `IDGenerated/manifest.json`
    bool ReadManifest(const CaseRoot& root, JsonReader& reader) noexcept;

    /// @brief 读 `IDGenerated/build.json`（顶层数组）
    bool ReadBuildRecords(const CaseRoot& root, JsonReader& reader) noexcept;

    /**
     *  @brief 在登记文件里查一个项目条目
     *  @param is_initialized 命中时回填 `is_initialized`
     *  @param total 回填登记文件里的条目总数
     *  @return 命中返回 true
     */
    bool FindRegistryEntry(const CaseRoot& root, std::string_view name, bool& is_initialized,
                           std::size_t& total) noexcept;

    /// @brief 登记文件里是否存在该项目名
    bool HasRegistryEntry(const CaseRoot& root, std::string_view name) noexcept;

    /// @brief 记录文件路径：`<projectRoot>/IDGenerated/<file>`
    std::string ProjectRecordPath(const CaseRoot& root, std::string_view file) noexcept;

    /// @brief 登记文件路径：`<engineRoot>/ID/project.json`
    std::string RegistryPath(const CaseRoot& root) noexcept;

    /// @brief 构建产物的统一 profile 参数（`--generator "MinGW Makefiles" --toolchain mingw --arch x64`）
    void AppendMingwProfile(std::vector<std::string>& arguments) noexcept;

    /// @brief 产物路径：`<projectRoot>/bin/<toolchain>/<arch>/<config>/<exe>`
    std::string ArtifactPath(const CaseRoot& root, std::string_view toolchain, std::string_view arch,
                             std::string_view config, std::string_view exe_name) noexcept;
} // namespace ID::Bench::IdProject
