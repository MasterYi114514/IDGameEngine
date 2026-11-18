#include "id_cmake_test_support.hpp"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include "benchmark_support.hpp"

/**
 *  id.cmake 域共享辅助实现：期望文本拼接、块切分、首个差异行与诊断定位断言。
 *
 *  文本比对口径（与计划书 §5.7 一致）：
 *  - 一个 CMake 文件自顶向下由"块"组成，块内不插空行、块与块之间恰有一个空行；
 *    因此 `SplitBlocks` 按连续空行切块，块内容去掉结尾换行后逐字节比较。
 *  - 全文比较用 `JoinLines`（每行后补 '\n'，空串表示空行），块比较用 `JoinBlock`（末尾不带 '\n'）。
 *  - 失败时 `Difference` 给出首个差异行号与两侧原文，避免只看到"文本不同"。
 *
 *  诊断口径（与计划书 §5.8 一致）：每条诊断按"类别 + 对象"断言，不看条数总和——
 *  同一目标既可能只报一条（判序互斥），也可能独立报多条（例如 ImportTargetSetting 与 ImportTargetScope）。
 */

namespace
{
    std::vector<std::string> SplitLines(std::string_view text)
    {
        std::vector<std::string> lines;
        std::size_t              begin = 0;
        while (begin < text.size())
        {
            const std::size_t end = text.find('\n', begin);
            if (end == std::string_view::npos)
            {
                lines.emplace_back(text.substr(begin));
                break;
            }
            lines.emplace_back(text.substr(begin, end - begin));
            begin = end + 1;
        }
        return lines;
    }

    void TrimTrailingNewlines(std::string& text)
    {
        while (!text.empty() && text.back() == '\n')
        {
            text.pop_back();
        }
    }
} // namespace

namespace ID::Bench::CmakeFixture
{
    std::string JoinLines(std::initializer_list<std::string_view> lines)
    {
        std::string text;
        for (const std::string_view line : lines)
        {
            text.append(line);
            text.push_back('\n');
        }
        return text;
    }

    std::string JoinBlock(std::initializer_list<std::string_view> lines)
    {
        std::string text;
        bool        is_first = true;
        for (const std::string_view line : lines)
        {
            if (!is_first)
            {
                text.push_back('\n');
            }
            text.append(line);
            is_first = false;
        }
        return text;
    }

    std::vector<std::string> SplitBlocks(std::string_view text)
    {
        std::vector<std::string> blocks;
        std::size_t              begin = 0;
        while (begin < text.size())
        {
            const std::size_t end = text.find("\n\n", begin);
            if (end == std::string_view::npos)
            {
                std::string block(text.substr(begin));
                TrimTrailingNewlines(block);
                if (!block.empty())
                {
                    blocks.push_back(std::move(block));
                }
                break;
            }

            std::string block(text.substr(begin, end - begin));
            TrimTrailingNewlines(block);
            if (!block.empty())
            {
                blocks.push_back(std::move(block));
            }
            begin = end + 2;
        }
        return blocks;
    }

    std::size_t BlockIndex(const std::vector<std::string>& blocks, std::string_view prefix) noexcept
    {
        for (std::size_t index = 0; index < blocks.size(); ++index)
        {
            if (blocks[index].compare(0, prefix.size(), prefix) == 0)
            {
                return index;
            }
        }
        return blocks.size();
    }

    std::string Difference(std::string_view actual, std::string_view expected)
    {
        if (actual == expected)
        {
            return "文本完全一致";
        }

        const std::vector<std::string> actual_lines   = SplitLines(actual);
        const std::vector<std::string> expected_lines = SplitLines(expected);
        const std::size_t              line_count     = (actual_lines.size() > expected_lines.size()) ? actual_lines.size()
                                                                                                     : expected_lines.size();
        for (std::size_t index = 0; index < line_count; ++index)
        {
            const std::string actual_line   = (index < actual_lines.size()) ? actual_lines[index] : std::string("<无此行>");
            const std::string expected_line = (index < expected_lines.size()) ? expected_lines[index] : std::string("<无此行>");
            if (actual_line != expected_line)
            {
                return "首个差异在第 " + std::to_string(index + 1) + " 行：期望 [" + expected_line + "] / 实际 [" +
                       actual_line + "]（实际 " + std::to_string(actual.size()) + " B / 期望 " +
                       std::to_string(expected.size()) + " B）";
            }
        }
        return "行内容一致但字节数不同（实际 " + std::to_string(actual.size()) + " B / 期望 " +
               std::to_string(expected.size()) + " B）";
    }

    std::size_t CountText(std::string_view text, std::string_view needle) noexcept
    {
        if (needle.empty() || needle.size() > text.size())
        {
            return 0;
        }

        std::size_t count = 0;
        std::size_t index = 0;
        while ((index = text.find(needle, index)) != std::string_view::npos)
        {
            ++count;
            index += needle.size();
        }
        return count;
    }

    std::string DescribeReport(const ID::CMake::ValidationReport& report)
    {
        if (report.diagnostics.empty())
        {
            return std::string("无诊断（is_valid=") + (report.is_valid ? "true" : "false") + "）";
        }

        std::string text = std::string("is_valid=") + (report.is_valid ? "true" : "false") + "；诊断 " +
                           std::to_string(report.diagnostics.size()) + " 条：";
        for (const ID::CMake::Diagnostic& diagnostic : report.diagnostics)
        {
            text += " [";
            text.append(ID::CMake::to_string(diagnostic.error));
            text += " subject=";
            text += diagnostic.subject;
            text += " location=";
            text += diagnostic.location;
            text += "]";
        }
        return text;
    }

    std::size_t CountDiagnostics(const ID::CMake::ValidationReport& report,
                                 ID::CMake::CMakeError             error,
                                 std::string_view                  subject) noexcept
    {
        std::size_t count = 0;
        for (const ID::CMake::Diagnostic& diagnostic : report.diagnostics)
        {
            if (diagnostic.error == error && std::string_view(diagnostic.subject) == subject)
            {
                ++count;
            }
        }
        return count;
    }

    std::size_t CountDiagnostics(const ID::CMake::ValidationReport& report, ID::CMake::CMakeError error) noexcept
    {
        std::size_t count = 0;
        for (const ID::CMake::Diagnostic& diagnostic : report.diagnostics)
        {
            if (diagnostic.error == error)
            {
                ++count;
            }
        }
        return count;
    }

    void CheckDiagnostic(const ID::CMake::ValidationReport& report,
                         ID::CMake::CMakeError             error,
                         std::string_view                  expected_subject,
                         std::string_view                  case_name) noexcept
    {
        const bool is_found = CountDiagnostics(report, error, expected_subject) > 0;
        ID::Bench::Check(is_found,
                         case_name,
                         std::string("期望类别 ") + std::string(ID::CMake::to_string(error)) +
                             " 且 subject=[" + std::string(expected_subject) + "]；实际：" + DescribeReport(report));
        ID::Bench::Check(!report.is_valid,
                         std::string(case_name) + ".report_invalid",
                         "出现诊断时 ValidationReport::is_valid 必须为 false");
    }

    void CheckText(std::string_view case_name, std::string_view actual, std::string_view expected) noexcept
    {
        ID::Bench::Check(actual == expected, case_name, Difference(actual, expected));
    }

    bool ReadWholeFile(const std::string& path, std::string& out) noexcept
    {
        out.clear();
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }

        char        buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            out.append(buffer, read);
        }

        std::fclose(file);
        return true;
    }

    bool WriteWholeFile(const std::string& path, std::string_view bytes) noexcept
    {
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            return false;
        }

        const bool is_written = (bytes.empty()) ? true : (std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
        std::fclose(file);
        return is_written;
    }

    TempProjectRoot::TempProjectRoot(std::string_view case_name)
        : m_path(ID::Bench::TempPath(std::string("id/cmake/") + std::string(case_name)))
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path(m_path), error);
        std::filesystem::create_directories(std::filesystem::path(m_path), error);
    }

    TempProjectRoot::~TempProjectRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path(m_path), error);
    }

    const std::string& TempProjectRoot::path() const noexcept
    {
        return m_path;
    }
} // namespace ID::Bench::CmakeFixture
