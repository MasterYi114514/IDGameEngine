#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark_support.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Directory/DirectoryEmitter.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/TargetType.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 目录层用例（计划书 §5.5 `Directory/` 与 §5.7 的 `emit_directory` 输出算法）。
 *
 *  判定依据：
 *  - 整文件形态：第 1 项 `cmake_minimum_required(VERSION 4.1.0)` → 空行 → 第 2 项 `project(<名>[ LANGUAGES ...])`
 *    → 空行 → 第 3 项 `option(...)` 块 → 第 4 项配置块 → 各目标块（第 5–12 项，块间空一行）→ 第 13 项；
 *    项与项之间恰有一个空行，块内不插空行。
 *  - 配置块内部顺序固定：`set(...)` 变量（`is_cache` 追加 `CACHE STRING "<doc>"`）→ `add_compile_options(...)`
 *    （相邻同条件并入同一 `if`）→ 各形态可切换目标的库类型选择块（属第 4 项，写在源通配与 `add_library` 之前）
 *    → 配置块命令（块末尾）。
 *  - 目录体形态（`BodyOnly`）：只有第 3–13 项，不含 `cmake_minimum_required` 与 `project`；
 *    与整文件形态的差别就只是这一段落。
 *  - 注释头：非空 `GeneratedHeader` 时在首行之前写两行注释（`# 由 <工具名> 生成，请勿手工修改` 与
 *    `# 来源：<resolved 记录>`），其后空一行再写第 1 项；取值按 `Argument::get_text()` 原样写出（不经参数渲染）。
 *  - 第 13 项：两种子目录入口共用一条有序序列，`add_subdirectory` 按条目顺序交错输出，尾部命令排在全部
 *    `add_subdirectory` 之后；跨类固定"子目录在前、尾部命令在后"，不做跨类排序。
 *
 *  纪律：模型自建（`Project` / `Directory` 的构造入口只在本层），`emit_*` 只产文本、不落盘。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::CmakeFixture::BlockIndex;
    using ID::Bench::CmakeFixture::CheckText;
    using ID::Bench::CmakeFixture::CountText;
    using ID::Bench::CmakeFixture::JoinBlock;
    using ID::Bench::CmakeFixture::JoinLines;
    using ID::Bench::CmakeFixture::SplitBlocks;
    using ID::CMake::Argument;
    using ID::CMake::Directory;
    using ID::CMake::EmitScope;
    using ID::CMake::emit_directory;
    using ID::CMake::GeneratedHeader;
    using ID::CMake::Project;
    using ID::CMake::Scope;
    using ID::CMake::TargetType;

    // 一个覆盖全部目录层元素的代表模型：project 语言、option、普通变量与 CACHE 变量、带条件的全局编译选项、
    // 形态可切换目标（含互斥宏块与同条件普通条目）、两类子目录、尾部命令
    void BuildDemoModel(Project& project)
    {
        Directory& root = project.root();
        root.add_language(Argument::bare("CXX"));
        root.add_option(Argument::bare("DEMO_SHARED"), false, Argument::quoted("构建共享库"));
        root.add_variable(Argument::bare("DEMO_OUTPUT_DIR"),
                          {Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin")},
                          false,
                          Argument::quoted(""));
        root.add_variable(Argument::bare("DEMO_FEATURES"), {Argument::bare("ON")}, true, Argument::quoted("特性开关"));
        root.add_global_compile_option(Argument::bare("-Wall"));
        root.add_global_compile_option(Argument::bare("-Wextra"), Argument::verbatim("MSVC"));
        root.add_global_compile_option(Argument::bare("-W4"), Argument::verbatim("MSVC"));
        root.add_configuration_command(Argument::verbatim("message(STATUS \"配置块\")"));

        ID::CMake::Target& target = root.create_target("DemoCore", TargetType::StaticLibrary);
        target.set_source_glob_dir("src");
        target.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin"));
        target.set_form_option(Argument::bare("DEMO_SHARED"),
                               Argument::bare("DEMO_SHARED_BUILD"),
                               Argument::bare("DEMO_STATIC_BUILD"));
        target.add_compile_definition(Scope::Private, Argument::bare("DEMO_SHARED_MARK"), Argument::verbatim("DEMO_SHARED"));

        root.create_subdirectory("A");
        root.create_referenced_subdirectory("Ext");
        root.add_trailing_command(Argument::verbatim("message(STATUS \"尾部\")"));
    }

    std::string DemoExpectedText()
    {
        return JoinLines({
            "cmake_minimum_required(VERSION 4.1.0)",
            "",
            "project(Demo LANGUAGES CXX)",
            "",
            "option(DEMO_SHARED \"构建共享库\" OFF)",
            "",
            "set(DEMO_OUTPUT_DIR \"${CMAKE_SOURCE_DIR}/bin\")",
            "set(DEMO_FEATURES ON CACHE STRING \"特性开关\")",
            "add_compile_options(-Wall)",
            "if(MSVC)",
            "    add_compile_options(-Wextra -W4)",
            "endif()",
            "if(DEMO_SHARED)",
            "    set(DEMOCORE_LIBRARY_TYPE SHARED)",
            "else()",
            "    set(DEMOCORE_LIBRARY_TYPE STATIC)",
            "endif()",
            "message(STATUS \"配置块\")",
            "",
            "file(GLOB_RECURSE DEMOCORE_SRC_FILES CONFIGURE_DEPENDS",
            "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp",
            "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp",
            ")",
            "",
            "add_library(DemoCore ${DEMOCORE_LIBRARY_TYPE} ${DEMOCORE_SRC_FILES})",
            "",
            "if(DEMO_SHARED)",
            "    target_compile_definitions(DemoCore",
            "        PRIVATE",
            "            DEMO_SHARED_BUILD",
            "    )",
            "else()",
            "    target_compile_definitions(DemoCore",
            "        PUBLIC",
            "            DEMO_STATIC_BUILD",
            "    )",
            "endif()",
            "if(DEMO_SHARED)",
            "    target_compile_definitions(DemoCore",
            "        PRIVATE",
            "            DEMO_SHARED_MARK",
            "    )",
            "endif()",
            "",
            "target_compile_features(DemoCore",
            "    PUBLIC",
            "        cxx_std_20",
            ")",
            "",
            "set_target_properties(DemoCore PROPERTIES",
            "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    CXX_EXTENSIONS OFF",
            ")",
            "",
            "add_subdirectory(A)",
            "add_subdirectory(Ext)",
            "message(STATUS \"尾部\")",
        });
    }

    // ==================== 整文件与目录体 ====================

    void CaseDirectoryWholeFile() noexcept
    {
        Project project("Demo");
        BuildDemoModel(project);

        const std::string whole = emit_directory(project.root());
        CheckText("id.cmake.directory.whole_file.canonical", whole, DemoExpectedText());
        Check(CountText(whole, "\n\n\n") == 0, "id.cmake.directory.whole_file.single_blank", "项与项之间恰有一个空行");

        // 目录体形态：只去掉第 1、2 项这一段落
        Project body_project("Demo");
        BuildDemoModel(body_project);
        const std::string body = emit_directory(body_project.root(), EmitScope::BodyOnly);
        const std::string preamble = JoinLines({
            "cmake_minimum_required(VERSION 4.1.0)",
            "",
            "project(Demo LANGUAGES CXX)",
            "",
        });
        CheckText("id.cmake.directory.body_only.is_whole_minus_first_items", whole, preamble + body);
        Check(body.find("cmake_minimum_required") == std::string::npos, "id.cmake.directory.body_only.no_minimum_required",
              "目录体形态不含第 1 项 cmake_minimum_required");
        Check(body.find("project(") == std::string::npos, "id.cmake.directory.body_only.no_project",
              "目录体形态不含第 2 项 project");
    }

    void CaseDirectoryGeneratedHeader() noexcept
    {
        Project project("Demo");
        BuildDemoModel(project);

        const GeneratedHeader header{Argument::quoted("ID"), Argument::quoted_expand("docs/resolved 记录.json")};
        const std::string     with_header = emit_directory(project.root(), EmitScope::WholeFile, &header);
        const std::string header_text = JoinLines({
            "# 由 ID 生成，请勿手工修改",
            "# 来源：docs/resolved 记录.json",
            "",
        });

        CheckText("id.cmake.directory.generated_header.two_lines", with_header, header_text + emit_directory(project.root()));
        Check(with_header.find("# 由 \"ID\" 生成") == std::string::npos, "id.cmake.directory.generated_header.raw_text",
              "注释头按 get_text() 原样写出，不经参数渲染（不加引号、不转义）");
        Check(CountText(with_header, "# 来源：") == 1, "id.cmake.directory.generated_header.source_once",
              "来源注释恰好一行");
    }

    // ==================== project() 行与 option 默认值 ====================

    void CaseDirectoryProjectLine() noexcept
    {
        Project    project("Plain");
        Directory& root = project.root();
        root.add_option(Argument::bare("PLAIN_ON"), true, Argument::quoted("默认开启"));
        root.add_option(Argument::bare("PLAIN_OFF"), false, Argument::quoted("默认关闭"));

        const std::string text = emit_directory(root);
        Check(text.find("project(Plain)\n") != std::string::npos, "id.cmake.directory.project.no_languages",
              "add_language 无值时省略 LANGUAGES 段");
        Check(text.find("LANGUAGES") == std::string::npos, "id.cmake.directory.project.no_languages_keyword",
              "省略 LANGUAGES 时关键字也不出现");
        Check(text.find("option(PLAIN_ON \"默认开启\" ON)") != std::string::npos, "id.cmake.directory.option.default_on",
              "is_default_on 为真写 ON");
        Check(text.find("option(PLAIN_OFF \"默认关闭\" OFF)") != std::string::npos, "id.cmake.directory.option.default_off",
              "is_default_on 为假写 OFF");

        const std::vector<std::string> blocks = SplitBlocks(text);
        const std::size_t              options_index = BlockIndex(blocks, "option(");
        Check(options_index < blocks.size(), "id.cmake.directory.option.block_present", "option 块存在");
        if (options_index < blocks.size())
        {
            CheckText("id.cmake.directory.option.block_no_blank", blocks[options_index], JoinBlock({
                "option(PLAIN_ON \"默认开启\" ON)",
                "option(PLAIN_OFF \"默认关闭\" OFF)",
            }));
        }
    }

    // ==================== 第 13 项交错顺序与尾部命令 ====================

    void CaseDirectorySubdirectoryOrder() noexcept
    {
        Project    project("Order");
        Directory& root = project.root();
        root.create_subdirectory("A");
        root.create_referenced_subdirectory("Ext");
        root.create_subdirectory("B");
        root.add_trailing_command(Argument::verbatim("message(STATUS \"第一条尾部命令\")"));
        root.add_trailing_command(Argument::verbatim("message(STATUS \"第二条尾部命令\")"));

        const std::string              text   = emit_directory(root);
        const std::vector<std::string> blocks = SplitBlocks(text);
        const std::size_t              index  = BlockIndex(blocks, "add_subdirectory(");
        Check(index < blocks.size(), "id.cmake.directory.trailing.block_present", "第 13 项块存在");
        if (index < blocks.size())
        {
            CheckText("id.cmake.directory.trailing.order", blocks[index], JoinBlock({
                "add_subdirectory(A)",
                "add_subdirectory(Ext)",
                "add_subdirectory(B)",
                "message(STATUS \"第一条尾部命令\")",
                "message(STATUS \"第二条尾部命令\")",
            }));
        }
        Check(CountText(text, "add_subdirectory(") == 3, "id.cmake.directory.trailing.subdirectory_count",
              "两种入口的条目都进同一条序列，共 3 行 add_subdirectory");
    }

    // ==================== 目标块分隔 ====================

    void CaseDirectoryTargetBlocks() noexcept
    {
        Project    project("Two");
        Directory& root = project.root();
        root.create_target("One", TargetType::InterfaceLibrary);
        root.create_target("TwoT", TargetType::InterfaceLibrary);

        const std::string text = emit_directory(root);
        Check(text.find("add_library(One INTERFACE)\n\nadd_library(TwoT INTERFACE)\n") != std::string::npos,
              "id.cmake.directory.target_blocks.single_blank",
              "两个目标块之间恰有一个空行（同一块内不插空行）");
        Check(CountText(text, "\n\n\n") == 0, "id.cmake.directory.target_blocks.no_double_blank",
              "不出现连续两个空行");
    }
    void CaseBodyOnlyWithHeaderUncovered() noexcept
    {
        // 契约（计划书 §5.6）：`BodyOnly` 与非空 `GeneratedHeader` 同时出现属调用方违约，由 ID_ASSERT 终止进程；
        // 同进程触发会带走整个测试进程，因此不作运行期用例，只登记障碍。
        ID::Bench::Uncovered("id.cmake.directory.body_only_with_header",
                             "契约要求 ID_ASSERT 终止进程（受控片段内不写注释头，计划书 §5.6）；id.cmake 子域"
                             "没有子进程分裂，不改被测实现无法受控取证");
    }
} // namespace

namespace ID::Bench
{
    void RunCmakeDirectoryCases() noexcept
    {
        CaseDirectoryWholeFile();
        CaseDirectoryGeneratedHeader();
        CaseDirectoryProjectLine();
        CaseDirectorySubdirectoryOrder();
        CaseDirectoryTargetBlocks();
        CaseBodyOnlyWithHeaderUncovered();
    }
} // namespace ID::Bench
