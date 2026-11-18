#include <cstddef>
#include <string>
#include <string_view>

#include "benchmark_support.hpp"
#include "cmake/Common/Writer.hpp"
#include "cmake/Value/Argument.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 值层与文本层用例（计划书 §5.2 `Value/Argument.hpp`、§5.3 `Common/Writer.hpp`）。
 *
 *  判定依据（公开头顶部注释 + 计划书正文）：
 *  - `Bare` 原样输出；`Quoted` 加引号并转义 `"`、`\`、`$`；`QuotedExpand` 加引号但只转义 `"`、`\`
 *    （保留 `$` 的变量展开）；`Verbatim` 原样、不加引号、不转义。
 *  - `is_valid_bare_argument` 只服务裸值：文本非空且全部字符落在 `A-Z a-z 0-9 _ - . / : + = @ ,`
 *    之内才为 true；`Quoted` / `QuotedExpand` / `Verbatim` 一律 true（字符集判定不覆盖它们）。
 *  - `Writer` 缩进单位固定 4 空格；`push_line` 空文本只写换行（不带前导空格）；
 *    `push_unindented` 顶格；`push_blank_line` 恒写一个换行；`reset` 清空缓冲并把缩进归零。
 *  - `end_scope` 在缩进为 0 时调用属调用方违约，由 `ID_ASSERT` 终止进程——同进程触发会带走整个测试进程，
 *    id.cmake 子域不建子进程分裂，因此按 `[UNCOVERED]` 登记障碍，不伪造通过。
 *
 *  纪律：每个用例自包含地新建 `Argument` / `Writer`，失败用 `Check` 记录并继续。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::CmakeFixture::CheckText;
    using ID::Bench::CmakeFixture::JoinLines;
    using ID::CMake::Argument;
    using ID::CMake::ArgumentKind;
    using ID::CMake::Writer;
    using ID::CMake::is_valid_bare_argument;
    using ID::CMake::render_argument;

    // ==================== 值层 ====================

    void CaseRenderArgument() noexcept
    {
        // Bare：原样输出，不加引号、不转义
        Check(render_argument(Argument::bare("IDWindow")) == "IDWindow", "id.cmake.render.bare.plain",
              "Bare 原样写出，不加引号");
        Check(render_argument(Argument::bare("cxx_std_20")) == "cxx_std_20", "id.cmake.render.bare.feature",
              "Bare 承载编译特性名时原样写出");
        Check(Argument::bare("a$b").get_kind() == ArgumentKind::Bare, "id.cmake.render.bare.kind",
              "bare() 构造出的类别必须是 Bare（不因文本内容改类别）");

        // Quoted：加引号并转义 '"'、'\\'、'$'（$ 变成字面量）
        Check(render_argument(Argument::quoted("C:/Program Files")) == "\"C:/Program Files\"", "id.cmake.render.quoted.plain",
              "Quoted 两端加引号，空格不需要额外转义");
        Check(render_argument(Argument::quoted("a$b")) == "\"a\\$b\"", "id.cmake.render.quoted.dollar_escaped",
              "Quoted 把 $ 转义成字面量（禁止变量展开）");
        Check(render_argument(Argument::quoted("q\"x")) == "\"q\\\"x\"", "id.cmake.render.quoted.quote_escaped",
              "Quoted 转义双引号");
        Check(render_argument(Argument::quoted("b\\s")) == "\"b\\\\s\"", "id.cmake.render.quoted.backslash_escaped",
              "Quoted 转义反斜杠，且转义产物不再二次转义");

        // QuotedExpand：加引号但只转义 '"'、'\\'，保留 '$'
        Check(render_argument(Argument::quoted_expand("${CMAKE_CURRENT_SOURCE_DIR}/third party/include")) ==
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/third party/include\"",
              "id.cmake.render.quoted_expand.variable_kept",
              "QuotedExpand 保留变量展开且整体作为一个参数（计划书 §5.2 的第三格）");
        Check(render_argument(Argument::quoted_expand("a$b")) == "\"a$b\"", "id.cmake.render.quoted_expand.dollar_kept",
              "QuotedExpand 不转义 $");
        Check(render_argument(Argument::quoted_expand("q\"x")) == "\"q\\\"x\"", "id.cmake.render.quoted_expand.quote_escaped",
              "QuotedExpand 仍转义双引号");
        Check(render_argument(Argument::quoted_expand("b\\s")) == "\"b\\\\s\"", "id.cmake.render.quoted_expand.backslash_escaped",
              "QuotedExpand 仍转义反斜杠");

        // 空文本：空引号是合法 CMake 参数（契约允许空串，空串不承担语义）
        Check(render_argument(Argument::quoted("")) == "\"\"", "id.cmake.render.quoted.empty", "Quoted 允许空文本，渲染成空引号");
        Check(render_argument(Argument::quoted_expand("")) == "\"\"", "id.cmake.render.quoted_expand.empty",
              "QuotedExpand 允许空文本，渲染成空引号");

        // Verbatim：原样，不加引号、不转义（只用于原始词法片段位置）
        Check(render_argument(Argument::verbatim("if(WIN32)")) == "if(WIN32)", "id.cmake.render.verbatim.condition",
              "Verbatim 原样写出，用于条件等原始词法片段");
        Check(render_argument(Argument::verbatim("if(${X} STREQUAL \"a b\")")) == "if(${X} STREQUAL \"a b\")",
              "id.cmake.render.verbatim.raw", "Verbatim 不转义 $ 与引号");

        // get_text 返回原始文本，不是渲染结果
        Check(Argument::quoted("a$b").get_text() == "a$b", "id.cmake.render.get_text_raw",
              "get_text() 返回渲染前的原始文本");
        Check(Argument::verbatim("a\nb").get_kind() == ArgumentKind::Verbatim, "id.cmake.render.verbatim.multiline_kind",
              "Verbatim 承载多行文本时类别不变（多行只允许出现在两个逃生口位置）");
    }

    void CaseBareArgumentCharset() noexcept
    {
        // 正例：字符集全集（一条）与逐字符（每个字符独立一条，证明整张表都放行）
        constexpr std::string_view kLegalChars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-./:+=@,";
        Check(is_valid_bare_argument(Argument::bare(kLegalChars)), "id.cmake.bare.legal.all_chars",
              "字符集全集 A-Z a-z 0-9 _ - . / : + = @ , 组成的裸值合法");
        for (const char symbol : kLegalChars)
        {
            const char text[2] = {symbol, '\0'};
            Check(is_valid_bare_argument(Argument::bare(text)),
                  std::string("id.cmake.bare.legal.single.") + symbol,
                  "字符集内的单个字符合法");
        }

        // 正例：实际取值形态
        Check(is_valid_bare_argument(Argument::bare("IDJson")), "id.cmake.bare.legal.identifier", "标识符形态的裸值合法");
        Check(is_valid_bare_argument(Argument::bare("cxx_std_20")), "id.cmake.bare.legal.compile_feature",
              "cxx_std_20 形态的裸值合法");
        Check(is_valid_bare_argument(Argument::bare("src/Hpp")), "id.cmake.bare.legal.relative_path",
              "相对路径形态的裸值合法");
        Check(is_valid_bare_argument(Argument::bare("-Wall")), "id.cmake.bare.legal.flag", "编译选项形态的裸值合法");

        // 负例：空串、空格、中文、$、引号与其它字符集外字符
        struct InvalidCase
        {
            std::string_view text;
            std::string_view name;
            std::string_view reason;
        };
        const InvalidCase invalid_cases[] = {
            {"", "empty", "裸值为空串时必须拒绝"},
            {"has space", "space", "裸值含空格必须拒绝"},
            {"中文", "chinese", "裸值含中文必须拒绝"},
            {"$X", "dollar", "裸值含 $ 必须拒绝（需要展开时用 QuotedExpand）"},
            {"a\"b", "double_quote", "裸值含双引号必须拒绝"},
            {"a;b", "semicolon", "裸值含分号必须拒绝（CMake 会按列表重新分词）"},
            {"a#b", "hash", "裸值含 # 必须拒绝"},
            {"a*b", "asterisk", "裸值含 * 必须拒绝"},
            {"a(b)", "parenthesis", "裸值含括号必须拒绝"},
            {"a\tb", "tab", "裸值含制表符必须拒绝"},
            {"a\nb", "newline", "裸值含换行必须拒绝"},
            {"${CMAKE_SOURCE_DIR}/bin", "variable", "裸值含 ${...} 必须拒绝（用 QuotedExpand）"},
        };
        for (const InvalidCase& item : invalid_cases)
        {
            Check(!is_valid_bare_argument(Argument::bare(item.text)),
                  std::string("id.cmake.bare.invalid.") + std::string(item.name),
                  item.reason);
        }

        // 非 Bare 类别不受字符集约束：函数只服务裸值
        Check(is_valid_bare_argument(Argument::quoted("has space")), "id.cmake.bare.skip.quoted",
              "Quoted 一律返回 true（转义由渲染负责，字符集只约束裸值）");
        Check(is_valid_bare_argument(Argument::quoted_expand("")), "id.cmake.bare.skip.quoted_expand_empty",
              "QuotedExpand 空文本也返回 true（空引号合法）");
        Check(is_valid_bare_argument(Argument::verbatim("if(WIN32)")), "id.cmake.bare.skip.verbatim",
              "Verbatim 一律返回 true（原始词法片段不受裸值字符集约束）");
    }

    // ==================== 文本层 ====================

    void CaseWriterIndent() noexcept
    {
        Writer writer;
        Check(writer.size() == 0, "id.cmake.writer.new.size", "新建 Writer 的缓冲为空");
        Check(writer.view().empty(), "id.cmake.writer.new.view", "新建 Writer 的视图为空");

        writer.push_line("first");
        CheckText("id.cmake.writer.push_line.top_level", writer.view(), JoinLines({"first"}));

        writer.begin_scope();
        writer.push_line("second");
        CheckText("id.cmake.writer.push_line.indent_one", writer.view(), JoinLines({"first", "    second"}));

        writer.begin_scope();
        writer.push_line("third");
        CheckText("id.cmake.writer.push_line.indent_two", writer.view(), JoinLines({"first", "    second", "        third"}));

        // 空文本行：只写换行，不写前导空格（否则与 push_blank_line 的产物不一致）
        writer.push_line("");
        CheckText("id.cmake.writer.push_line.empty_text", writer.view(),
                  JoinLines({"first", "    second", "        third", ""}));

        // 顶格：`)` / `if` / `else` / `endif` 一类必须从行首开始
        writer.push_unindented(")");
        CheckText("id.cmake.writer.push_unindented.top", writer.view(),
                  JoinLines({"first", "    second", "        third", "", ")"}));

        writer.push_unindented("");
        CheckText("id.cmake.writer.push_unindented.empty_text", writer.view(),
                  JoinLines({"first", "    second", "        third", "", ")", ""}));

        writer.push_blank_line();
        CheckText("id.cmake.writer.push_blank_line", writer.view(),
                  JoinLines({"first", "    second", "        third", "", ")", "", ""}));

        writer.end_scope();
        writer.end_scope();
        writer.push_line("tail");
        CheckText("id.cmake.writer.end_scope.twice", writer.view(),
                  JoinLines({"first", "    second", "        third", "", ")", "", "", "tail"}));

        Check(writer.size() == writer.view().size(), "id.cmake.writer.size.matches_view",
              "size() 与 view().size() 必须一致（同一个缓冲的字节数）");
        Check(Writer::kIndentWidth == 4, "id.cmake.writer.indent_width",
              "缩进单位固定 4 空格（计划书 §5.3：不使用 Tab）");
    }

    void CaseWriterReset() noexcept
    {
        Writer writer;
        writer.begin_scope();
        writer.push_line("a");
        writer.reset();
        Check(writer.size() == 0, "id.cmake.writer.reset.size", "reset() 清空缓冲");
        Check(writer.view().empty(), "id.cmake.writer.reset.view", "reset() 后视图为空");

        writer.push_line("b");
        CheckText("id.cmake.writer.reset.indent_zero", writer.view(), JoinLines({"b"}));

        writer.reset();
        writer.reset();
        Check(writer.size() == 0, "id.cmake.writer.reset.idempotent", "连续 reset() 幂等，不产生残留字节");

        writer.push_unindented("c");
        CheckText("id.cmake.writer.reset.after_unindented", writer.view(), JoinLines({"c"}));
    }

    void CaseWriterEndScopeUnderflowUncovered() noexcept
    {
        // 契约（计划书 §5.3）：缩进为 0 时调用 end_scope 属调用方破坏内部不变量，由 ID_ASSERT 终止进程。
        // 同进程触发会带走整个测试进程，因此本次不作运行期用例，只登记障碍。
        ID::Bench::Uncovered("id.cmake.writer.end_scope_underflow",
                             "契约要求 ID_ASSERT 终止进程（计划书 §5.3）；id.cmake 子域没有子进程分裂，"
                             "同进程触发会终止整个测试进程，不改被测实现无法受控取证");
    }
} // namespace

namespace ID::Bench
{
    void RunCmakeValueCases() noexcept
    {
        CaseRenderArgument();
        CaseBareArgumentCharset();
    }

    void RunCmakeTextCases() noexcept
    {
        CaseWriterIndent();
        CaseWriterReset();
        CaseWriterEndScopeUnderflowUncovered();
    }
} // namespace ID::Bench
