#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark_support.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/TargetEmitter.hpp"
#include "cmake/Target/TargetType.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 目标层用例（计划书 §5.7 的 emitter 输出算法、§5.11 的 IMPORTED 目标契约）。
 *
 *  判定依据：
 *  - 第 5–12 项顺序与空行：`file(GLOB_RECURSE ...)` → `add_library`/`add_executable` →
 *    `target_compile_definitions` → `target_compile_features` → `set_target_properties` →
 *    `target_include_directories` → `target_link_directories` → `target_link_libraries`；
 *    相邻两项之间恰有一个空行，同一项内不插空行。
 *  - 第 11 项 `target_link_directories` 两态：有 link 目录条目时按第 7 项同规则输出（作用域 4 空格、
 *    取值 8 空格、`)` 顶格），无条目时整块省略。
 *  - 作用域分段：同一条命令内作用域变化处重写关键字并新开缩进段，条目按插入顺序、不重排、不合并。
 *  - 条件 run：相邻且条件文本相同的条目并入同一个 `if(<cond>)` … `endif()`；被别的条件隔开的同条件条目各自成块；
 *    无条件条目直接展开，不包 `if`。
 *  - `SYSTEM`：是命令级选项，只写在该命令第一个作用域段之前（`SYSTEM <作用域>`），
 *    include 条目的 `is_system` 变化时另开一条同命令，其它三类语句永不输出 `SYSTEM`。
 *  - 形态选项两段：互斥宏块固定在第 7 项之首（`if` / `else()` / `endif()`），同条件的普通条目独立成 run 块，
 *    不并入 `else()`；库类型选择块属第 4 项（由 `emit_directory` 产出，见目录层用例）。
 *  - IMPORTED：第 5 项源通配整块省略、第 8 项编译特性整块省略、第 9 项只写 `IMPORTED_LOCATION` /
 *    `IMPORTED_IMPLIB`（不写输出目录属性、不写 `CXX_EXTENSIONS`），`GLOBAL` 只在 `is_global` 为真时出现，
 *    条目作用域恒 `INTERFACE`。
 *
 *  纪律：每个用例自建 `Project` 与目标（`Target` 只能由 `Directory::create_target` 构造）。
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
    using ID::CMake::emit_target;
    using ID::CMake::Project;
    using ID::CMake::Scope;
    using ID::CMake::Target;
    using ID::CMake::TargetType;

    // 收源目标：非 INTERFACE/imported 目标必须调 set_source_glob_dir("src")，emit_target 才能产出第 5 项
    Target& CreateSourceTarget(Project& project, std::string_view name, TargetType type)
    {
        Target& target = project.root().create_target(name, type);
        if (type != TargetType::InterfaceLibrary)
        {
            target.set_source_glob_dir("src");
        }
        return target;
    }

    /// @brief 断言"以 prefix 开头的块"与期望文本逐字节一致；块缺失时直接判失败并给出实际块数
    void CheckBlock(std::string_view                  case_name,
                    const std::vector<std::string>&   blocks,
                    std::string_view                  prefix,
                    std::string_view                  expected)
    {
        const std::size_t index = BlockIndex(blocks, prefix);
        if (index >= blocks.size())
        {
            Check(false, case_name,
                  std::string("未找到以 [") + std::string(prefix) + "] 开头的块；实际块数=" +
                      std::to_string(blocks.size()));
            return;
        }
        CheckText(case_name, blocks[index], expected);
    }

    // ==================== 第 5–12 项骨架 ====================

    void CaseTargetSkeleton() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "Sample", TargetType::StaticLibrary);
        target.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin"));
        target.add_compile_definition(Scope::Public, Argument::bare("SAMPLE_ON"));
        target.add_include_directory(Scope::Private,
                                     Argument::quoted_expand("${CMAKE_CURRENT_SOURCE_DIR}/include"));
        target.add_link_library(Scope::Private, Argument::bare("IDLog"));

        const std::string text = emit_target(target);
        const std::string expected = JoinLines({
            "file(GLOB_RECURSE SAMPLE_SRC_FILES CONFIGURE_DEPENDS",
            "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp",
            "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp",
            ")",
            "",
            "add_library(Sample STATIC ${SAMPLE_SRC_FILES})",
            "",
            "target_compile_definitions(Sample",
            "    PUBLIC",
            "        SAMPLE_ON",
            ")",
            "",
            "target_compile_features(Sample",
            "    PUBLIC",
            "        cxx_std_20",
            ")",
            "",
            "set_target_properties(Sample PROPERTIES",
            "    ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    CXX_EXTENSIONS OFF",
            ")",
            "",
            "target_include_directories(Sample",
            "    PRIVATE",
            "        \"${CMAKE_CURRENT_SOURCE_DIR}/include\"",
            ")",
            "",
            "target_link_libraries(Sample",
            "    PRIVATE",
            "        IDLog",
            ")",
        });
        CheckText("id.cmake.target.skeleton.canonical", text, expected);

        // 块间恰一个空行：任意连续两个空行都说明分组口径错了
        Check(CountText(text, "\n\n\n") == 0, "id.cmake.target.skeleton.single_blank", "第 5–12 项之间恰有一个空行，不出现连续空行");

        // 项顺序（同时校验每项都存在，npos 会让比较失败）
        const std::size_t glob      = text.find("file(GLOB_RECURSE");
        const std::size_t add       = text.find("add_library(");
        const std::size_t defs      = text.find("target_compile_definitions(");
        const std::size_t features  = text.find("target_compile_features(");
        const std::size_t props     = text.find("set_target_properties(");
        const std::size_t includes  = text.find("target_include_directories(");
        const std::size_t links     = text.find("target_link_libraries(");
        Check(glob != std::string::npos && add != std::string::npos && defs != std::string::npos
                  && features != std::string::npos && props != std::string::npos && includes != std::string::npos
                  && links != std::string::npos,
              "id.cmake.target.skeleton.items_present", "第 5、6、7、8、9、10、12 项都应出现");
        Check(glob < add && add < defs && defs < features && features < props && props < includes && includes < links,
              "id.cmake.target.skeleton.item_order", "第 5–12 项按骨架顺序出现（file → add → definitions → features → properties → include → link）");
        Check(text.find("target_link_directories(") == std::string::npos,
              "id.cmake.target.skeleton.item11_omitted", "没有 link 目录条目时第 11 项整块省略（不写空命令）");
    }

    // ==================== 第 11 项 target_link_directories ====================

    /// @brief 第 11 项的正向形态：有计划书要求的命中位置与语句缩进，且位于第 10、12 项之间
    void CaseTargetLinkDirectories() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "Linked", TargetType::StaticLibrary);
        target.add_include_directory(Scope::Private, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));
        target.add_link_directory(Scope::Private, Argument::bare("lib/win64"));
        target.add_link_directory(Scope::Public, Argument::bare("lib/common"));
        target.add_link_library(Scope::Private, Argument::bare("IDLog"));

        const std::string              text   = emit_target(target);
        const std::vector<std::string> blocks = SplitBlocks(text);

        // 与第 7、10、12 项同规则：作用域 4 空格、取值 8 空格、`)` 顶格；作用域变化处重写关键字并新开
        // 缩进段，条目按插入顺序（不按作用域重排、不合并）
        CheckBlock("id.cmake.target.link_directories.block", blocks, "target_link_directories(", JoinBlock({
            "target_link_directories(Linked",
            "    PRIVATE",
            "        lib/win64",
            "    PUBLIC",
            "        lib/common",
            ")",
        }));

        // 缩进口径单列一条：命令首行与 `)` 从行首开始、作用域层 4 空格、取值层 8 空格
        Check(CountText(text, "\n    PRIVATE\n        lib/win64\n") == 1, "id.cmake.target.link_directories.indent",
              "作用域 4 空格、取值 8 空格；`)` 顶格由块比对覆盖");

        // 骨架位置：第 11 项在第 10 项之后、第 12 项之前（计划书 §5.7 的第 10–12 项顺序）
        const std::size_t includes    = text.find("target_include_directories(");
        const std::size_t directories = text.find("target_link_directories(");
        const std::size_t libraries   = text.find("target_link_libraries(");
        Check(includes != std::string::npos && directories != std::string::npos && libraries != std::string::npos,
              "id.cmake.target.link_directories.items_present", "第 10、11、12 项都应出现");
        Check(includes < directories && directories < libraries, "id.cmake.target.link_directories.item_order",
              "target_link_directories 位于 target_include_directories 之后、target_link_libraries 之前");
        Check(text.find("\n)\n\ntarget_link_libraries(") != std::string::npos,
              "id.cmake.target.link_directories.closing_paren",
              "第 11 项的 `)` 顶格，且与第 12 项之间恰有一个空行");

        // 第 11 项不输出 SYSTEM：那是 include 条目的命令级选项（计划书 §5.7 的 SYSTEM 条）
        Check(text.find("SYSTEM") == std::string::npos, "id.cmake.target.link_directories.no_system",
              "link 目录语句不输出 SYSTEM");
    }

    // ==================== 作用域分段 ====================

    void CaseTargetScopeSegments() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "Scoped", TargetType::StaticLibrary);
        target.add_compile_definition(Scope::Public, Argument::bare("PUBLIC_A"));
        target.add_compile_definition(Scope::Private, Argument::bare("PRIVATE_B"));
        target.add_compile_definition(Scope::Public, Argument::bare("PUBLIC_C"));

        const std::vector<std::string> blocks = SplitBlocks(emit_target(target));
        CheckBlock("id.cmake.target.scope.segments", blocks, "target_compile_definitions(", JoinBlock({
            "target_compile_definitions(Scoped",
            "    PUBLIC",
            "        PUBLIC_A",
            "    PRIVATE",
            "        PRIVATE_B",
            "    PUBLIC",
            "        PUBLIC_C",
            ")",
        }));
    }

    // ==================== 条件 run 合并 ====================

    void CaseTargetConditionRuns() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "Cond", TargetType::StaticLibrary);
        target.add_compile_definition(Scope::Public, Argument::bare("WIN32_A"), Argument::verbatim("WIN32"));
        target.add_compile_definition(Scope::Public, Argument::bare("WIN32_B"), Argument::verbatim("WIN32"));
        target.add_compile_definition(Scope::Public, Argument::bare("APPLE_ONLY"), Argument::verbatim("APPLE"));
        target.add_compile_definition(Scope::Public, Argument::bare("WIN32_C"), Argument::verbatim("WIN32"));
        target.add_compile_definition(Scope::Public, Argument::bare("ALWAYS_ON"));

        const std::vector<std::string> blocks = SplitBlocks(emit_target(target));
        const std::string expected = JoinBlock({
            "if(WIN32)",
            "    target_compile_definitions(Cond",
            "        PUBLIC",
            "            WIN32_A",
            "            WIN32_B",
            "    )",
            "endif()",
            "if(APPLE)",
            "    target_compile_definitions(Cond",
            "        PUBLIC",
            "            APPLE_ONLY",
            "    )",
            "endif()",
            "if(WIN32)",
            "    target_compile_definitions(Cond",
            "        PUBLIC",
            "            WIN32_C",
            "    )",
            "endif()",
            "target_compile_definitions(Cond",
            "    PUBLIC",
            "        ALWAYS_ON",
            ")",
        });
        CheckBlock("id.cmake.target.condition.runs", blocks, "if(WIN32)", expected);

        if (const std::size_t index = BlockIndex(blocks, "if(WIN32)"); index < blocks.size())
        {
            Check(CountText(blocks[index], "if(WIN32)") == 2, "id.cmake.target.condition.split_blocks",
                  "被 APPLE 隔开的两段 WIN32 条目各自成块（相邻同条件才并入同一 if）");
            Check(CountText(blocks[index], "endif()") == 3, "id.cmake.target.condition.endif_count",
                  "三个 if 块各有一个 endif()");
            Check(CountText(blocks[index], "target_compile_definitions(Cond") == 4, "id.cmake.target.condition.command_count",
                  "三个条件块各一条命令、无条件段一条命令，共 4 条");
        }

        // 无条件 run 不包 if：单独一条命令
        Project plain("Proj");
        Target& plain_target = CreateSourceTarget(plain, "Plain", TargetType::StaticLibrary);
        plain_target.add_compile_definition(Scope::Public, Argument::bare("ONLY"));
        const std::string plain_text = emit_target(plain_target);
        Check(plain_text.find("if(") == std::string::npos, "id.cmake.target.condition.unconditional_no_if",
              "没有条件条目时不产出 if(...)");
    }

    // ==================== SYSTEM 独立命令 ====================

    void CaseTargetSystemCommand() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "Sys", TargetType::StaticLibrary);
        target.add_include_directory(Scope::Private, Argument::bare("private_a"), std::nullopt, false);
        target.add_include_directory(Scope::Public, Argument::bare("system_b"), std::nullopt, true);
        target.add_include_directory(Scope::Public, Argument::bare("system_c"), std::nullopt, true);
        target.add_include_directory(Scope::Private, Argument::bare("private_d"), std::nullopt, false);

        const std::vector<std::string> blocks = SplitBlocks(emit_target(target));
        const std::string expected = JoinBlock({
            "target_include_directories(Sys",
            "    PRIVATE",
            "        private_a",
            ")",
            "target_include_directories(Sys",
            "    SYSTEM PUBLIC",
            "        system_b",
            "        system_c",
            ")",
            "target_include_directories(Sys",
            "    PRIVATE",
            "        private_d",
            ")",
        });
        CheckBlock("id.cmake.target.system.separate_commands", blocks, "target_include_directories(", expected);

        const std::string text = emit_target(target);
        Check(CountText(text, "SYSTEM") == 1, "id.cmake.target.system.single_keyword",
              "SYSTEM 是命令级选项，只在含 system 条目那条命令的第一个作用域段前出现一次");
        Check(CountText(text, "\n    SYSTEM PUBLIC\n") == 1, "id.cmake.target.system.keyword_before_scope",
              "SYSTEM 与作用域关键字同行、SYSTEM 在前，且处于作用域关键字那一层缩进（4 空格）");
    }

    // ==================== 形态选项两段位置 ====================

    void CaseTargetFormOption() noexcept
    {
        Project project("Proj");
        Directory& root = project.root();
        root.add_option(Argument::bare("MOD_SHARED"), false, Argument::quoted("构建共享库"));
        Target& target = CreateSourceTarget(project, "Mod", TargetType::StaticLibrary);
        target.set_form_option(Argument::bare("MOD_SHARED"),
                               Argument::bare("MOD_SHARED_DEFINE"),
                               Argument::bare("MOD_STATIC_DEFINE"));
        target.add_compile_definition(Scope::Public, Argument::bare("MOD_SHARED_MARK"), Argument::verbatim("MOD_SHARED"));

        const std::string              text   = emit_target(target);
        const std::vector<std::string> blocks = SplitBlocks(text);

        // 第 6 项引用形态派生变量，而不是写死 STATIC/SHARED
        CheckBlock("id.cmake.target.form_option.add_line", blocks, "add_library(", JoinBlock({
            "add_library(Mod ${MOD_LIBRARY_TYPE} ${MOD_SRC_FILES})",
        }));

        // 第 7 项：互斥宏块固定 if/else/endif；同条件的普通条目在其后独立成块，不并入 else()
        CheckBlock("id.cmake.target.form_option.item7", blocks, "if(MOD_SHARED)", JoinBlock({
            "if(MOD_SHARED)",
            "    target_compile_definitions(Mod",
            "        PRIVATE",
            "            MOD_SHARED_DEFINE",
            "    )",
            "else()",
            "    target_compile_definitions(Mod",
            "        PUBLIC",
            "            MOD_STATIC_DEFINE",
            "    )",
            "endif()",
            "if(MOD_SHARED)",
            "    target_compile_definitions(Mod",
            "        PUBLIC",
            "            MOD_SHARED_MARK",
            "    )",
            "endif()",
        }));
        Check(CountText(text, "else()") == 1, "id.cmake.target.form_option.else_only_form",
              "else() 只服务形态选项的两个互斥分支");
    }

    // ==================== IMPORTED 目标 ====================

    void CaseTargetImported() noexcept
    {
        Project project("Proj");
        Directory& root = project.root();

        // 共享库 imported：IMPORTED_LOCATION + IMPORTED_IMPLIB + GLOBAL，条目作用域 INTERFACE
        Target& imported = root.create_target("Imp", TargetType::SharedLibrary);
        imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.dll"),
                            Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.dll.a"), true);
        imported.add_include_directory(Scope::Interface, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));

        const std::string              imported_text = emit_target(imported);
        const std::vector<std::string> blocks        = SplitBlocks(imported_text);
        CheckBlock("id.cmake.target.imported.add_line", blocks, "add_library(", JoinBlock({
            "add_library(Imp SHARED IMPORTED GLOBAL)",
        }));
        CheckBlock("id.cmake.target.imported.properties", blocks, "set_target_properties(", JoinBlock({
            "set_target_properties(Imp PROPERTIES",
            "    IMPORTED_LOCATION \"${CMAKE_SOURCE_DIR}/lib/libImp.dll\"",
            "    IMPORTED_IMPLIB \"${CMAKE_SOURCE_DIR}/lib/libImp.dll.a\"",
            ")",
        }));
        CheckBlock("id.cmake.target.imported.entries", blocks, "target_include_directories(", JoinBlock({
            "target_include_directories(Imp",
            "    INTERFACE",
            "        \"${CMAKE_SOURCE_DIR}/include\"",
            ")",
        }));
        Check(imported_text.find("file(GLOB_RECURSE") == std::string::npos, "id.cmake.target.imported.no_glob",
              "imported 目标不写第 5 项源通配");
        Check(imported_text.find("target_compile_features") == std::string::npos, "id.cmake.target.imported.no_features",
              "imported 目标不参与编译，第 8 项整块省略");
        Check(imported_text.find("CXX_EXTENSIONS") == std::string::npos, "id.cmake.target.imported.no_cxx_extensions",
              "imported 目标的第 9 项不写 CXX_EXTENSIONS");
        Check(imported_text.find("OUTPUT_DIRECTORY") == std::string::npos, "id.cmake.target.imported.no_output_directory",
              "imported 目标不写输出目录属性");

        // INTERFACE imported：不写 location/implib、不带 GLOBAL
        Target& interface_imported = root.create_target("ImpI", TargetType::InterfaceLibrary);
        interface_imported.set_import(std::nullopt, std::nullopt, false);
        CheckText("id.cmake.target.imported.interface", emit_target(interface_imported), JoinLines({
            "add_library(ImpI INTERFACE IMPORTED)",
        }));

        // 可执行 imported：没有源文件列表，GLOBAL 紧随 IMPORTED
        Target& executable_imported = root.create_target("ImpE", TargetType::Executable);
        executable_imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin/imp.exe"), std::nullopt, true);
        const std::vector<std::string> executable_blocks = SplitBlocks(emit_target(executable_imported));
        CheckBlock("id.cmake.target.imported.executable", executable_blocks, "add_executable(", JoinBlock({
            "add_executable(ImpE IMPORTED GLOBAL)",
        }));

        // 不带 GLOBAL 的 imported：GLOBAL 只在 is_global 为真时出现
        Target& local_imported = root.create_target("ImpL", TargetType::StaticLibrary);
        local_imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.a"), std::nullopt, false);
        const std::string local_text = emit_target(local_imported);
        Check(local_text.find("GLOBAL") == std::string::npos, "id.cmake.target.imported.global_absent",
              "is_global 为假时不写 GLOBAL");
    }

    // ==================== INTERFACE 库 ====================

    void CaseTargetInterfaceLibrary() noexcept
    {
        Project project("Proj");
        Directory& root = project.root();

        Target& interface_target = root.create_target("Iface", TargetType::InterfaceLibrary);
        interface_target.add_include_directory(Scope::Interface, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));

        const std::string              text   = emit_target(interface_target);
        const std::vector<std::string> blocks = SplitBlocks(text);
        CheckBlock("id.cmake.target.interface.add_line", blocks, "add_library(", JoinBlock({
            "add_library(Iface INTERFACE)",
        }));
        CheckBlock("id.cmake.target.interface.entries", blocks, "target_include_directories(", JoinBlock({
            "target_include_directories(Iface",
            "    INTERFACE",
            "        \"${CMAKE_SOURCE_DIR}/include\"",
            ")",
        }));
        Check(text.find("file(GLOB_RECURSE") == std::string::npos, "id.cmake.target.interface.no_glob",
              "INTERFACE 库没有编译单元，不写第 5 项源通配");
        Check(text.find("target_compile_features") == std::string::npos, "id.cmake.target.interface.no_features",
              "INTERFACE 库不写第 8 项编译特性");
        Check(text.find("set_target_properties") == std::string::npos, "id.cmake.target.interface.no_properties",
              "INTERFACE 库不产生产物，第 9 项整块省略");
    }

    // ==================== 输出目录属性按类型展开 ====================

    void CaseTargetOutputDirectory() noexcept
    {
        Project project("Proj");
        const Argument directory = Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin");

        Target& executable = CreateSourceTarget(project, "App", TargetType::Executable);
        executable.set_output_directory(directory);
        CheckBlock("id.cmake.target.output.executable", SplitBlocks(emit_target(executable)), "set_target_properties(", JoinBlock({
            "set_target_properties(App PROPERTIES",
            "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    CXX_EXTENSIONS OFF",
            ")",
        }));

        Target& shared = CreateSourceTarget(project, "Sh", TargetType::SharedLibrary);
        shared.set_output_directory(directory);
        CheckBlock("id.cmake.target.output.shared", SplitBlocks(emit_target(shared)), "set_target_properties(", JoinBlock({
            "set_target_properties(Sh PROPERTIES",
            "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    CXX_EXTENSIONS OFF",
            ")",
        }));

        Target& static_library = CreateSourceTarget(project, "St", TargetType::StaticLibrary);
        static_library.set_output_directory(directory);
        CheckBlock("id.cmake.target.output.static", SplitBlocks(emit_target(static_library)), "set_target_properties(", JoinBlock({
            "set_target_properties(St PROPERTIES",
            "    ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/bin\"",
            "    CXX_EXTENSIONS OFF",
            ")",
        }));

        // 未设置输出目录：不写任何输出属性，但仍写 CXX_EXTENSIONS OFF
        Target& no_output = CreateSourceTarget(project, "NoOut", TargetType::StaticLibrary);
        const std::string no_output_text = emit_target(no_output);
        CheckBlock("id.cmake.target.output.absent", SplitBlocks(no_output_text), "set_target_properties(", JoinBlock({
            "set_target_properties(NoOut PROPERTIES",
            "    CXX_EXTENSIONS OFF",
            ")",
        }));
        Check(no_output_text.find("OUTPUT_DIRECTORY") == std::string::npos, "id.cmake.target.output.absent_no_property",
              "未调用 set_output_directory 时不写任何输出目录属性");
    }

    // ==================== 编译特性作用域 ====================

    void CaseTargetCompileFeaturesScope() noexcept
    {
        Project project("Proj");

        Target& executable = CreateSourceTarget(project, "App", TargetType::Executable);
        CheckBlock("id.cmake.target.features.executable", SplitBlocks(emit_target(executable)), "target_compile_features(", JoinBlock({
            "target_compile_features(App",
            "    PRIVATE",
            "        cxx_std_20",
            ")",
        }));

        Target& library = CreateSourceTarget(project, "Lib", TargetType::StaticLibrary);
        library.set_compile_standard(Argument::bare("cxx_std_20"));
        CheckBlock("id.cmake.target.features.library", SplitBlocks(emit_target(library)), "target_compile_features(", JoinBlock({
            "target_compile_features(Lib",
            "    PUBLIC",
            "        cxx_std_20",
            ")",
        }));
    }
} // namespace

namespace ID::Bench
{
    void RunCmakeTargetCases() noexcept
    {
        CaseTargetSkeleton();
        CaseTargetLinkDirectories();
        CaseTargetScopeSegments();
        CaseTargetConditionRuns();
        CaseTargetSystemCommand();
        CaseTargetFormOption();
        CaseTargetImported();
        CaseTargetInterfaceLibrary();
        CaseTargetOutputDirectory();
        CaseTargetCompileFeaturesScope();
    }
} // namespace ID::Bench
