#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "benchmark_support.hpp"
#include "cmake/Common/Diagnostic.hpp"
#include "cmake/File/FragmentWriter.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 落盘层用例（计划书 §5.8 的写入协议与受控片段边界规则）。
 *
 *  判定依据：
 *  - 整文件写入：创建（父目录按需创建）→ 内容与现有文件相同即幂等跳过（成功但 `is_written=false`、`bytes=0`、
 *    无诊断）→ 内容不同则整体替换；成败只看 `is_success`（`diagnostic` 空态），不看 `is_written`。
 *  - 受控片段：只替换 begin/end 两标记行之间的字节，标记外字节逐字保留（含既有 UTF-8 BOM 与既有行尾）；
 *    片段按文件第一个换行符决定的既有行尾展开（CRLF 文件写出 CRLF，禁止整文件行尾归一化）；
 *    片段不以换行结尾时补一个既有行尾，保证 end 标记从行首开始；内容未变化即幂等跳过。
 *  - 六类写入期诊断：`MissingFile`（目标不存在，不创建）、`MissingMarker`（标记缺失或不独占一行）、
 *    `AmbiguousMarker`（标记重复或 begin 在 end 之后）、`PathEscapesProjectRoot`（`..` 逃逸与项目外绝对路径）、
 *    `IoFailed`（目标是目录等打开/写入失败）、`TargetModified`（替换前重读发现目标被改动）。
 *    前三类失败不写入、不追加、不创建父目录——用例用"文件字节逐字不变"取证。
 *  - `TargetModified` 需要"读取与替换之间目标被改写"的竞态：不改被测实现无法受控注入（写入路径内部完成
 *    重读与改名），因此按 `[UNCOVERED]` 登记障碍，不伪造通过。
 *
 *  纪律：每个用例在 `<临时目录>/id/cmake/<用例名>` 这个独立项目根内自建前置文件，不触碰仓库里的真实文件。
 */

namespace
{
    using ID::Bench::CmakeFixture::CheckText;
    using ID::Bench::CmakeFixture::CountText;
    using ID::Bench::CmakeFixture::JoinLines;
    using ID::Bench::CmakeFixture::ReadWholeFile;
    using ID::Bench::CmakeFixture::TempProjectRoot;
    using ID::Bench::CmakeFixture::WriteWholeFile;
    using ID::Bench::Check;
    using ID::CMake::FragmentOutcome;
    using ID::CMake::FragmentWriter;
    using ID::CMake::is_success;

    // 受控片段标记：由组装阶段提供，本测试用固定取值（不含 CR/LF，各自独占一行）
    constexpr char kBeginMarker[] = "# >>> ID GENERATED BEGIN";
    constexpr char kEndMarker[]   = "# <<< ID GENERATED END";

    // 既有 BOM 属标记外字节：只识别、不求值，原样保留
    constexpr char kUtf8Bom[] = "\xEF\xBB\xBF";

    std::string PathIn(const std::string& root, std::string_view relative)
    {
        std::string path = root;
        path.push_back('/');
        path.append(relative);
        return path;
    }

    std::string ReadBack(const std::string& path)
    {
        std::string bytes;
        if (!ReadWholeFile(path, bytes))
        {
            return std::string("<读取失败>");
        }
        return bytes;
    }

    std::string DescribeOutcome(const FragmentOutcome& outcome)
    {
        std::string text = std::string("is_written=") + (outcome.is_written ? "true" : "false")
                         + " bytes=" + std::to_string(outcome.bytes);
        if (!outcome.diagnostic.has_value())
        {
            text += "；无诊断";
            return text;
        }

        text += "；诊断=[";
        text.append(ID::CMake::to_string(outcome.diagnostic->error));
        text += " subject=";
        text += outcome.diagnostic->subject;
        text += " expected=";
        text += outcome.diagnostic->expected;
        text += " actual=";
        text += outcome.diagnostic->actual;
        text += " location=";
        text += outcome.diagnostic->location;
        text += " next_action=";
        text += outcome.diagnostic->next_action;
        text += "]";
        return text;
    }

    bool CreateDirectory(const std::string& path)
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path), error);
        return std::filesystem::is_directory(std::filesystem::path(path), error);
    }

    bool PathExists(const std::string& path)
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::path(path), error);
    }

    // ==================== 整文件写入 ====================

    void CaseWholeFileWrite() noexcept
    {
        TempProjectRoot root("whole_file");
        FragmentWriter   writer(root.path());

        const std::string content = JoinLines({
            "cmake_minimum_required(VERSION 4.1.0)",
            "",
            "project(Generated)",
        });
        const std::string path = PathIn(root.path(), "Generated/CMakeLists.txt");

        const FragmentOutcome created = writer.write_whole_file("Generated/CMakeLists.txt", content);
        Check(is_success(created), "id.cmake.file.whole_file.create.success", DescribeOutcome(created));
        Check(created.is_written, "id.cmake.file.whole_file.create.written", DescribeOutcome(created));
        Check(created.bytes == content.size(), "id.cmake.file.whole_file.create.bytes",
              "bytes 应为写出内容的字节数；" + DescribeOutcome(created));
        Check(PathExists(path), "id.cmake.file.whole_file.create.parent_created", "父目录按需创建，文件已落盘");
        CheckText("id.cmake.file.whole_file.create.content", ReadBack(path), content);

        // 幂等：内容相同不重写，成功但未写入
        const FragmentOutcome again = writer.write_whole_file("Generated/CMakeLists.txt", content);
        Check(is_success(again), "id.cmake.file.whole_file.idempotent.success", DescribeOutcome(again));
        Check(!again.is_written, "id.cmake.file.whole_file.idempotent.not_written",
              "内容与现有文件相同即幂等跳过；" + DescribeOutcome(again));
        Check(again.bytes == 0, "id.cmake.file.whole_file.idempotent.bytes",
              "幂等跳过时 bytes 为 0；" + DescribeOutcome(again));
        Check(!again.diagnostic.has_value(), "id.cmake.file.whole_file.idempotent.no_diagnostic",
              "幂等跳过不产生诊断（is_success 为真）");

        // 替换：内容不同则整体替换
        const std::string replaced_content = JoinLines({
            "cmake_minimum_required(VERSION 4.1.0)",
            "",
            "project(Replaced)",
        });
        const FragmentOutcome replaced = writer.write_whole_file("Generated/CMakeLists.txt", replaced_content);
        Check(is_success(replaced) && replaced.is_written, "id.cmake.file.whole_file.replace.written", DescribeOutcome(replaced));
        CheckText("id.cmake.file.whole_file.replace.content", ReadBack(path), replaced_content);
    }

    // ==================== 受控片段：CRLF 与 BOM 保留 ====================

    void CaseManagedBlockCrlfBom() noexcept
    {
        TempProjectRoot root("managed_crlf_bom");
        const std::string directory = PathIn(root.path(), "Root");
        Check(CreateDirectory(directory), "id.cmake.file.managed_crlf.prepare_directory", "前置：建目标文件所在目录");

        const std::string path = PathIn(root.path(), "Root/CMakeLists.txt");
        const std::string existing = std::string(kUtf8Bom) + "# 用户头\r\n" + kBeginMarker + "\r\n# 旧片段\r\n"
                                   + kEndMarker + "\r\n# 用户尾\r\n";
        Check(WriteWholeFile(path, existing), "id.cmake.file.managed_crlf.prepare_file",
              "前置：写入含 BOM 与 CRLF 行尾的目标文件");

        FragmentWriter writer(root.path());
        const std::string fragment = "set(GENERATED_A 1)\nset(GENERATED_B 2)\n";
        const FragmentOutcome outcome =
            writer.write_managed_block("Root/CMakeLists.txt", fragment, kBeginMarker, kEndMarker);
        Check(is_success(outcome), "id.cmake.file.managed_crlf.success", DescribeOutcome(outcome));
        Check(outcome.is_written, "id.cmake.file.managed_crlf.written", DescribeOutcome(outcome));

        const std::string expected = std::string(kUtf8Bom) + "# 用户头\r\n" + kBeginMarker
                                   + "\r\nset(GENERATED_A 1)\r\nset(GENERATED_B 2)\r\n" + kEndMarker + "\r\n# 用户尾\r\n";
        const std::string actual = ReadBack(path);
        CheckText("id.cmake.file.managed_crlf.bytes", actual, expected);
        Check(outcome.bytes == expected.size(), "id.cmake.file.managed_crlf.bytes_field",
              "bytes 为写出的新文件字节数；" + DescribeOutcome(outcome));

        const bool is_bom_kept = actual.size() >= 3 && static_cast<unsigned char>(actual[0]) == 0xEF
                              && static_cast<unsigned char>(actual[1]) == 0xBB
                              && static_cast<unsigned char>(actual[2]) == 0xBF;
        Check(is_bom_kept, "id.cmake.file.managed_crlf.bom_kept", "既有 BOM 属标记外字节，原样保留");
        Check(CountText(actual, "set(GENERATED_A 1)\r\n") == 1, "id.cmake.file.managed_crlf.newline_crlf",
              "片段按文件既有行尾（CRLF）展开");
        Check(CountText(actual, "set(GENERATED_A 1)\n") == 0, "id.cmake.file.managed_crlf.no_bare_lf",
              "不混入裸 LF，禁止整文件行尾归一化");
        const std::string prefix = std::string(kUtf8Bom) + "# 用户头\r\n";
        Check(actual.size() > prefix.size() && actual.compare(0, prefix.size(), prefix) == 0,
              "id.cmake.file.managed_crlf.outside_preserved", "标记之前的用户区（含 BOM 与既有行尾）逐字保留");
        const std::string suffix = "# 用户尾\r\n";
        Check(actual.size() > suffix.size() && actual.compare(actual.size() - suffix.size(), suffix.size(), suffix) == 0,
              "id.cmake.file.managed_crlf.tail_preserved", "标记之后的用户区逐字保留");

        // 幂等：片段内容未变化即跳过
        const FragmentOutcome again =
            writer.write_managed_block("Root/CMakeLists.txt", fragment, kBeginMarker, kEndMarker);
        Check(is_success(again), "id.cmake.file.managed_crlf.idempotent.success", DescribeOutcome(again));
        Check(!again.is_written, "id.cmake.file.managed_crlf.idempotent.not_written", DescribeOutcome(again));
        CheckText("id.cmake.file.managed_crlf.idempotent.bytes", ReadBack(path), expected);
    }

    // ==================== 受控片段：LF、BOM 与片段末尾换行 ====================

    void CaseManagedBlockLfBom() noexcept
    {
        TempProjectRoot root("managed_lf_bom");
        const std::string path = PathIn(root.path(), "CMakeLists.txt");
        const std::string existing = std::string(kUtf8Bom) + "# 头\n" + kBeginMarker + "\n# 旧片段\n" + kEndMarker
                                   + "\n# 尾\n";
        Check(WriteWholeFile(path, existing), "id.cmake.file.managed_lf.prepare_file", "前置：写入含 BOM 与 LF 行尾的目标文件");

        FragmentWriter writer(root.path());
        // 片段不以换行结尾：写出的片段必须补一个既有行尾，让 end 标记从行首开始
        const std::string fragment = "set(A 1)\nset(B 2)";
        const FragmentOutcome outcome = writer.write_managed_block("CMakeLists.txt", fragment, kBeginMarker, kEndMarker);
        Check(is_success(outcome), "id.cmake.file.managed_lf.success", DescribeOutcome(outcome));

        const std::string expected = std::string(kUtf8Bom) + "# 头\n" + kBeginMarker
                                   + "\nset(A 1)\nset(B 2)\n" + kEndMarker + "\n# 尾\n";
        CheckText("id.cmake.file.managed_lf.bytes", ReadBack(path), expected);
        Check(CountText(ReadBack(path), "\r\n") == 0, "id.cmake.file.managed_lf.no_crlf", "LF 文件不引入 CRLF");
    }

    // ==================== MissingFile ====================

    void CaseMissingFile() noexcept
    {
        TempProjectRoot root("missing_file");
        FragmentWriter   writer(root.path());

        const FragmentOutcome outcome =
            writer.write_managed_block("Absent/CMakeLists.txt", "set(A 1)\n", kBeginMarker, kEndMarker);
        Check(!is_success(outcome), "id.cmake.file.missing_file.failed", DescribeOutcome(outcome));
        Check(outcome.diagnostic.has_value() && outcome.diagnostic->error == ID::CMake::CMakeError::MissingFile,
              "id.cmake.file.missing_file.category", DescribeOutcome(outcome));
        Check(outcome.diagnostic.has_value() && outcome.diagnostic->subject == "Absent/CMakeLists.txt",
              "id.cmake.file.missing_file.subject", DescribeOutcome(outcome));
        Check(!PathExists(PathIn(root.path(), "Absent")), "id.cmake.file.missing_file.not_created",
              "受控片段写入不创建文件，也不创建父目录");
    }

    // ==================== MissingMarker ====================

    void CaseMissingMarker() noexcept
    {
        TempProjectRoot root("missing_marker");
        FragmentWriter   writer(root.path());

        // (a) 完全没有标记
        const std::string no_marker = "# 头\nold\n# 尾\n";
        Check(WriteWholeFile(PathIn(root.path(), "NoMarker.txt"), no_marker), "id.cmake.file.missing_marker.prepare_a",
              "前置：写入无标记的目标文件");
        const FragmentOutcome no_marker_outcome =
            writer.write_managed_block("NoMarker.txt", "set(A 1)\n", kBeginMarker, kEndMarker);
        Check(no_marker_outcome.diagnostic.has_value()
                  && no_marker_outcome.diagnostic->error == ID::CMake::CMakeError::MissingMarker,
              "id.cmake.file.missing_marker.category", DescribeOutcome(no_marker_outcome));
        CheckText("id.cmake.file.missing_marker.unmodified", ReadBack(PathIn(root.path(), "NoMarker.txt")), no_marker);

        // (b) 只有 begin 标记
        const std::string only_begin = std::string(kBeginMarker) + "\nold\n";
        Check(WriteWholeFile(PathIn(root.path(), "OnlyBegin.txt"), only_begin), "id.cmake.file.missing_marker.prepare_b",
              "前置：只写 begin 标记");
        const FragmentOutcome only_begin_outcome =
            writer.write_managed_block("OnlyBegin.txt", "set(A 1)\n", kBeginMarker, kEndMarker);
        Check(only_begin_outcome.diagnostic.has_value()
                  && only_begin_outcome.diagnostic->error == ID::CMake::CMakeError::MissingMarker,
              "id.cmake.file.missing_marker.missing_end", DescribeOutcome(only_begin_outcome));
        // 措辞依据：计划书 §5.8 的受控片段边界规则只规定「begin 与 end 各自独占一行、必须恰好各一条」
        // 与 `actual` 须给出「实际」，未冻结措辞。下面这条按实现现状匹配它，用于证明诊断能指出缺的是哪一端
        // 标记，属措辞级断言；类别判定由上面的 category 承担。
        Check(only_begin_outcome.diagnostic.has_value()
                  && CountText(only_begin_outcome.diagnostic->actual, "缺 end 标记") > 0,
              "id.cmake.file.missing_marker.missing_end_text",
              "诊断的 actual 指明缺的是 end 标记；" + DescribeOutcome(only_begin_outcome));

        // (c) 标记不独占一行（行尾另有字符）
        const std::string marker_with_tail = "# 头\n" + std::string(kBeginMarker) + " 额外文字\n" + kEndMarker + "\n";
        Check(WriteWholeFile(PathIn(root.path(), "MarkerTail.txt"), marker_with_tail), "id.cmake.file.missing_marker.prepare_c",
              "前置：begin 标记行尾带额外文字");
        const FragmentOutcome marker_tail_outcome =
            writer.write_managed_block("MarkerTail.txt", "set(A 1)\n", kBeginMarker, kEndMarker);
        Check(marker_tail_outcome.diagnostic.has_value()
                  && marker_tail_outcome.diagnostic->error == ID::CMake::CMakeError::MissingMarker,
              "id.cmake.file.missing_marker.not_own_line", DescribeOutcome(marker_tail_outcome));
        CheckText("id.cmake.file.missing_marker.not_own_line_unmodified", ReadBack(PathIn(root.path(), "MarkerTail.txt")),
                  marker_with_tail);
    }

    // ==================== AmbiguousMarker ====================

    void CaseAmbiguousMarker() noexcept
    {
        TempProjectRoot root("ambiguous_marker");
        FragmentWriter   writer(root.path());

        // (a) begin 标记重复
        const std::string duplicated = std::string(kBeginMarker) + "\nold\n" + kBeginMarker + "\n" + kEndMarker + "\n";
        Check(WriteWholeFile(PathIn(root.path(), "Duplicated.txt"), duplicated), "id.cmake.file.ambiguous_marker.prepare_a",
              "前置：写入重复 begin 标记的目标文件");
        const FragmentOutcome duplicated_outcome =
            writer.write_managed_block("Duplicated.txt", "set(A 1)\n", kBeginMarker, kEndMarker);
        Check(duplicated_outcome.diagnostic.has_value()
                  && duplicated_outcome.diagnostic->error == ID::CMake::CMakeError::AmbiguousMarker,
              "id.cmake.file.ambiguous_marker.category", DescribeOutcome(duplicated_outcome));
        // 措辞依据：计划书 §5.8 只规定「标记重复或逆序报 AmbiguousMarker」与诊断四要素，未要求 `actual`
        // 报命中次数。下面这条按实现现状匹配该措辞，属附加定位信息（区分「重复」与「逆序」两种触发），
        // 不是契约取值。
        Check(duplicated_outcome.diagnostic.has_value()
                  && CountText(duplicated_outcome.diagnostic->actual, "begin 命中 2 次") > 0,
              "id.cmake.file.ambiguous_marker.actual_text",
              "诊断的 actual 给出两个标记各自的命中次数；" + DescribeOutcome(duplicated_outcome));
        CheckText("id.cmake.file.ambiguous_marker.unmodified", ReadBack(PathIn(root.path(), "Duplicated.txt")), duplicated);

        // (b) begin 在 end 之后
        const std::string reversed = std::string(kEndMarker) + "\nold\n" + kBeginMarker + "\n";
        Check(WriteWholeFile(PathIn(root.path(), "Reversed.txt"), reversed), "id.cmake.file.ambiguous_marker.prepare_b",
              "前置：写入 begin 在 end 之后的目标文件");
        const FragmentOutcome reversed_outcome =
            writer.write_managed_block("Reversed.txt", "set(A 1)\n", kBeginMarker, kEndMarker);
        Check(reversed_outcome.diagnostic.has_value()
                  && reversed_outcome.diagnostic->error == ID::CMake::CMakeError::AmbiguousMarker,
              "id.cmake.file.ambiguous_marker.reversed", DescribeOutcome(reversed_outcome));
        CheckText("id.cmake.file.ambiguous_marker.reversed_unmodified", ReadBack(PathIn(root.path(), "Reversed.txt")), reversed);
    }

    // ==================== PathEscapesProjectRoot ====================

    void CasePathEscapesProjectRoot() noexcept
    {
        TempProjectRoot root("path_escapes");
        FragmentWriter   writer(root.path());

        // (a) `..` 逃逸
        const FragmentOutcome parent_outcome = writer.write_whole_file("../escape.txt", "x\n");
        Check(!is_success(parent_outcome), "id.cmake.file.path_escapes.parent_failed", DescribeOutcome(parent_outcome));
        Check(parent_outcome.diagnostic.has_value()
                  && parent_outcome.diagnostic->error == ID::CMake::CMakeError::PathEscapesProjectRoot,
              "id.cmake.file.path_escapes.parent_category", DescribeOutcome(parent_outcome));
        Check(!PathExists(PathIn(root.path(), "../escape.txt")), "id.cmake.file.path_escapes.parent_not_written",
              "越界路径不产生任何文件");

        // (b) 项目外绝对路径
        std::error_code        error;
        const std::filesystem::path outside =
            std::filesystem::absolute(std::filesystem::path(PathIn(root.path(), "../abs_outside.txt")), error);
        Check(!error && !outside.empty(), "id.cmake.file.path_escapes.prepare_absolute",
              "前置：算出项目根之外的绝对路径");
        const FragmentOutcome absolute_outcome = writer.write_whole_file(outside.string(), "x\n");
        Check(!is_success(absolute_outcome), "id.cmake.file.path_escapes.absolute_failed", DescribeOutcome(absolute_outcome));
        Check(absolute_outcome.diagnostic.has_value()
                  && absolute_outcome.diagnostic->error == ID::CMake::CMakeError::PathEscapesProjectRoot,
              "id.cmake.file.path_escapes.absolute_category", DescribeOutcome(absolute_outcome));
        Check(!PathExists(outside.string()), "id.cmake.file.path_escapes.absolute_not_written",
              "项目外绝对路径不产生任何文件");
    }

    // ==================== IoFailed ====================

    void CaseIoFailed() noexcept
    {
        TempProjectRoot root("io_failed");
        FragmentWriter   writer(root.path());

        // 目标是已存在的目录：打开/读取阶段即失败，属 IoFailed（不改被测实现即可稳定构造）
        const std::string directory = PathIn(root.path(), "IsDirectory");
        Check(CreateDirectory(directory), "id.cmake.file.io_failed.prepare", "前置：建一个同名目录");

        const FragmentOutcome outcome = writer.write_whole_file("IsDirectory", "content\n");
        Check(!is_success(outcome), "id.cmake.file.io_failed.failed", DescribeOutcome(outcome));
        Check(outcome.diagnostic.has_value() && outcome.diagnostic->error == ID::CMake::CMakeError::IoFailed,
              "id.cmake.file.io_failed.category", DescribeOutcome(outcome));
        Check(outcome.diagnostic.has_value() && outcome.diagnostic->subject == "IsDirectory",
              "id.cmake.file.io_failed.subject", DescribeOutcome(outcome));
        const bool has_next_action = outcome.diagnostic.has_value() && !outcome.diagnostic->next_action.empty();
        Check(has_next_action, "id.cmake.file.io_failed.next_action", "失败时必须给出可执行的下一步动作");
        Check(PathExists(directory), "id.cmake.file.io_failed.directory_preserved", "失败不删除已存在的目录");
    }

    // ==================== TargetModified（无法受控注入） ====================

    void CaseTargetModifiedUncovered() noexcept
    {
        ID::Bench::Uncovered("id.cmake.file.target_modified",
                             "契约（计划书 §5.8 第 7 条）：write_managed_block 替换前重读目标，内容与先前读取不一致时"
                             "返回 TargetModified 且不替换。该竞态发生在被测实现内部的读取与改名之间，"
                             "不改被测实现无法受控注入（id.cmake 子域也没有跨进程改写同一文件的稳定注入点），"
                             "因此本次不作运行期用例，只登记障碍");
    }
} // namespace

namespace ID::Bench
{
    void RunCmakeFileCases() noexcept
    {
        CaseWholeFileWrite();
        CaseManagedBlockCrlfBom();
        CaseManagedBlockLfBom();
        CaseMissingFile();
        CaseMissingMarker();
        CaseAmbiguousMarker();
        CasePathEscapesProjectRoot();
        CaseIoFailed();
        CaseTargetModifiedUncovered();
    }
} // namespace ID::Bench
