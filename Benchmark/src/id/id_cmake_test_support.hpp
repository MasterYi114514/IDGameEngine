#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "cmake/Common/Diagnostic.hpp"

/**
 *  id.cmake 域的私有设施（不进公开 include 路径）：按被测层划分的用例入口与文本/诊断比对小工具。
 *
 *  为什么单独成头：正确性用例按层拆成多个 TU（值/文本、目标、目录、工程、validate、落盘），
 *  它们共用同一套"期望文本拼接 + 块切分 + 首个差异行"与"诊断类别 + 对象定位"断言工具；
 *  工具只有这一份实现，避免每个 TU 各自造一份比对口径。
 *
 *  纪律：本子域的用例入口全部是 `void() noexcept` 的普通函数，由 `IdCmakeCorrectnessTest` 顺序调用，
 *  不注册静态构造、不按调用顺序共享状态（每个用例自建模型与临时目录）。
 */

namespace ID::Bench
{
    // ---- 用例入口：按被测层拆分，每个函数自包含（不依赖调用顺序） ----

    /// @brief 值层：`render_argument` 四类渲染与转义、`is_valid_bare_argument` 字符集正负例
    void RunCmakeValueCases() noexcept;

    /// @brief 文本层：`Writer` 的 4 空格缩进、空文本行、顶格、嵌套作用域与 `reset`
    void RunCmakeTextCases() noexcept;

    /// @brief 目标层：第 5–12 项骨架与空行、作用域分段、条件 run 合并、SYSTEM 独立命令、
    ///        形态选项两段位置、IMPORTED 块与 INTERFACE 库
    void RunCmakeTargetCases() noexcept;

    /// @brief 目录层：整文件/目录体、option、变量（含 CACHE）、全局编译选项条件块、配置块命令、
    ///        目标块分隔、第 13 项两类子目录交错顺序与尾部命令、注释头
    void RunCmakeDirectoryCases() noexcept;

    /// @brief 工程层：`collect_outputs` 的路径与深度优先顺序、referenced 条目不落盘、`WholeFile` 默认、
    ///        目录遍历与外部目标登记
    void RunCmakeProjectCases() noexcept;

    /// @brief 生成前校验：`validate` 判定的 18 类诊断各至少一例、判序与互斥、正例零诊断
    void RunCmakeValidateCases() noexcept;

    /// @brief 落盘层：整文件创建/替换/幂等、受控片段 CRLF 与 BOM 保留、六类写入期诊断
    void RunCmakeFileCases() noexcept;
} // namespace ID::Bench

namespace ID::Bench::CmakeFixture
{
    /// @brief 按行拼出期望文本：每行后补 '\n'（emitter 产出恒以 '\n' 结尾，空串表示空行）
    std::string JoinLines(std::initializer_list<std::string_view> lines);

    /// @brief 按行拼出一个块（块间无空行）：用 '\n' 连接，末尾不带 '\n'
    std::string JoinBlock(std::initializer_list<std::string_view> lines);

    /// @brief 按空行切块（契约：块内不插空行、块间恰一个空行）；每块去掉结尾换行
    std::vector<std::string> SplitBlocks(std::string_view text);

    /// @brief 首个以 `prefix` 开头的块下标；没有时返回 `blocks.size()`
    std::size_t BlockIndex(const std::vector<std::string>& blocks, std::string_view prefix) noexcept;

    /// @brief 首个差异行（含两侧原文与行号），用于 `Check` 的 detail；完全相同时返回说明
    std::string Difference(std::string_view actual, std::string_view expected);

    /// @brief `needle` 在 `text` 中出现的次数（空 needle 返回 0）
    std::size_t CountText(std::string_view text, std::string_view needle) noexcept;

    /// @brief 逐条列出诊断（类别 + 对象 + 位置），用于断言失败时定位；空报告返回 "无诊断"
    std::string DescribeReport(const ID::CMake::ValidationReport& report);

    /// @brief 报告里"该类别 + 该对象"的诊断条数
    std::size_t CountDiagnostics(const ID::CMake::ValidationReport& report,
                                 ID::CMake::CMakeError             error,
                                 std::string_view                  subject) noexcept;

    /// @brief 报告里该类别（不看对象）的诊断条数
    std::size_t CountDiagnostics(const ID::CMake::ValidationReport& report,
                                 ID::CMake::CMakeError             error) noexcept;

    /// @brief 断言"存在该类别 + 该对象"的诊断，同时断言报告已被判为无效
    void CheckDiagnostic(const ID::CMake::ValidationReport& report,
                         ID::CMake::CMakeError             error,
                         std::string_view                  expected_subject,
                         std::string_view                  case_name) noexcept;

    /// @brief 字节级文本断言：失败时 detail 给出首个差异行
    void CheckText(std::string_view case_name, std::string_view actual, std::string_view expected) noexcept;

    // ---- 文件与临时项目根 ----

    /// @brief 读整个文件（二进制）；打不开或读不满时返回 false
    bool ReadWholeFile(const std::string& path, std::string& out) noexcept;

    /// @brief 写整个文件（二进制，父目录须已存在）
    bool WriteWholeFile(const std::string& path, std::string_view bytes) noexcept;

    /**
     *  本次用例独占的临时项目根：构造时清空并重建目录，析构时整目录删除。
     *  路径取 `<临时目录>/id/cmake/<用例名>`；用例名只用 ASCII，避免把本机代码页带进路径。
     */
    class TempProjectRoot
    {
    public:
        explicit TempProjectRoot(std::string_view case_name);
        ~TempProjectRoot();

        TempProjectRoot(const TempProjectRoot&)            = delete;
        TempProjectRoot& operator=(const TempProjectRoot&) = delete;

        /// @brief 项目根的路径文本（`<临时目录>/id/cmake/<用例名>`，用正斜杠连接；临时目录来自构建注入，与工作目录无关）
        const std::string& path() const noexcept;

    private:
        std::string m_path;
    };
} // namespace ID::Bench::CmakeFixture
