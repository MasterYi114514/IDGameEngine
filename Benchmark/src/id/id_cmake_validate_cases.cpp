#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "benchmark_support.hpp"
#include "cmake/Common/Diagnostic.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/TargetType.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 生成前校验用例（计划书 §5.8 的诊断类别表与判序、§5.11 的 imported 约束）。
 *
 *  覆盖口径：`validate(const Project&)` 判定的 18 类诊断各至少一例——
 *  EmptyName、InvalidName、InvalidBareArgument、InvalidTargetType、UnsupportedCompileStandard、
 *  InvalidSourceGlobDir、DuplicateTarget、DuplicateSubdirectory、DuplicateDerivedVariable、MissingSourceGlobDir、
 *  InterfaceTargetScope、InterfaceTargetSetting、InvalidFormOptionTarget、MissingImportLocation、
 *  ImportTargetSetting、ImportTargetScope、VerbatimArgument、LinkTargetNotFound。
 *  另有正例（合法工程零诊断）与 `to_string` 的 24 类类别名文本（CLI 的 `failed:<类别>` 依赖它）。
 *
 *  判序与互斥（每格独立取证，不靠"总数凑够"）：
 *  - `InvalidTargetType` 先判，之后不再报其它噪声；
 *  - `InvalidFormOptionTarget` 先于 `InterfaceTargetSetting`，同一目标在这两类之间只报一条；
 *  - `InterfaceTargetScope` 与 `ImportTargetScope` 按是否 imported 二选一；
 *  - `MissingSourceGlobDir` 与 `InterfaceTargetSetting` 对 imported 目标不适用。
 *
 *  断言口径：每条按"类别 + `subject` 对象"定位（不接受"只看到失败"），并核对关键 `location` / `actual`。
 *  纪律：每个用例自建 `Project`；`validate` 只读模型，不产文本、不写文件。
 */

namespace
{
    using ID::Bench::CmakeFixture::CheckDiagnostic;
    using ID::Bench::CmakeFixture::CountDiagnostics;
    using ID::Bench::CmakeFixture::CountText;
    using ID::Bench::CmakeFixture::DescribeReport;
    using ID::Bench::Check;
    using ID::CMake::Argument;
    using ID::CMake::CMakeError;
    using ID::CMake::Diagnostic;
    using ID::CMake::Directory;
    using ID::CMake::Project;
    using ID::CMake::Scope;
    using ID::CMake::Target;
    using ID::CMake::TargetType;
    using ID::CMake::validate;

    /// @brief 一条诊断的关键文本（失败时用于定位，不构成契约判定）
    struct DiagnosticText
    {
        bool        is_found = false;
        std::string expected = {};
        std::string actual   = {};
        std::string location = {};
    };

    DiagnosticText FindDiagnostic(const ID::CMake::ValidationReport& report,
                                  CMakeError                        error,
                                  std::string_view                  subject)
    {
        DiagnosticText result;
        for (const Diagnostic& diagnostic : report.diagnostics)
        {
            if (diagnostic.error == error && std::string_view(diagnostic.subject) == subject)
            {
                result.is_found = true;
                result.expected = diagnostic.expected;
                result.actual   = diagnostic.actual;
                result.location = diagnostic.location;
                break;
            }
        }
        return result;
    }

    Target& CreateSourceTarget(Project& project, std::string_view name, TargetType type)
    {
        Target& target = project.root().create_target(name, type);
        target.set_source_glob_dir("src");
        return target;
    }

    // ==================== 正例 ====================

    void CaseValidProject() noexcept
    {
        Project    project("Demo");
        Directory& root = project.root();
        root.add_language(Argument::bare("CXX"));
        root.add_option(Argument::bare("DEMO_SHARED"), false, Argument::quoted("构建共享库"));

        // 外部目标登记：链接项指向 IDPlugin 既有目标，四文件建模并不创建它们
        project.add_external_target(Argument::bare("IDLog"));
        project.add_external_target(Argument::bare("IDCommonHeaders"));

        Target& target = CreateSourceTarget(project, "DemoCore", TargetType::StaticLibrary);
        target.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin"));
        target.set_form_option(Argument::bare("DEMO_SHARED"),
                               Argument::bare("DEMO_SHARED_BUILD"),
                               Argument::bare("DEMO_STATIC_BUILD"));
        target.add_compile_definition(Scope::Public, Argument::bare("DEMO_ON"));
        target.add_include_directory(Scope::Private, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));
        target.add_link_library(Scope::Private, Argument::bare("IDLog"));

        Target& executable = CreateSourceTarget(project, "IDTool", TargetType::Executable);
        executable.add_link_library(Scope::Private, Argument::bare("IDCommonHeaders"));

        // 跨目录链接：子目录目标链接根目录已声明的目标名（判定范围是全工程）
        Directory& plugin = root.create_subdirectory("Plugin");
        Target&    plugin_target = plugin.create_target("PluginTarget", TargetType::StaticLibrary);
        plugin_target.set_source_glob_dir("src");
        plugin_target.add_link_library(Scope::Public, Argument::bare("DemoCore"));

        const ID::CMake::ValidationReport report = validate(project);
        Check(report.is_valid, "id.cmake.validate.valid.is_valid", DescribeReport(report));
        Check(report.diagnostics.empty(), "id.cmake.validate.valid.no_diagnostics", DescribeReport(report));
    }

    // ==================== 名称类 ====================

    void CaseEmptyName() noexcept
    {
        Project unnamed("");
        CheckDiagnostic(validate(unnamed), CMakeError::EmptyName, "project:", "id.cmake.validate.empty_name.project");

        Project project("Proj");
        CreateSourceTarget(project, "", TargetType::StaticLibrary);
        CheckDiagnostic(validate(project), CMakeError::EmptyName, "", "id.cmake.validate.empty_name.target");

        Project subdirectory_project("Proj");
        subdirectory_project.root().create_referenced_subdirectory("");
        CheckDiagnostic(validate(subdirectory_project), CMakeError::EmptyName, "subdirectory:",
                        "id.cmake.validate.empty_name.subdirectory");
    }

    void CaseInvalidName() noexcept
    {
        Project project("Proj");
        CreateSourceTarget(project, "9Bad", TargetType::StaticLibrary);
        CheckDiagnostic(validate(project), CMakeError::InvalidName, "9Bad", "id.cmake.validate.invalid_name.starts_with_digit");

        Project hyphen_project("Proj");
        CreateSourceTarget(hyphen_project, "Bad-Name", TargetType::StaticLibrary);
        CheckDiagnostic(validate(hyphen_project), CMakeError::InvalidName, "Bad-Name",
                        "id.cmake.validate.invalid_name.hyphen");

        Project subdirectory_project("Proj");
        subdirectory_project.root().create_referenced_subdirectory("bad name");
        CheckDiagnostic(validate(subdirectory_project), CMakeError::InvalidName, "subdirectory:bad name",
                        "id.cmake.validate.invalid_name.subdirectory");
    }

    // ==================== 参数形态类 ====================

    void CaseInvalidBareArgument() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "T", TargetType::StaticLibrary);
        target.add_compile_definition(Scope::Public, Argument::bare("has space"));
        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::InvalidBareArgument, "T:compile_definition",
                        "id.cmake.validate.invalid_bare_argument.entry_value");
        const DiagnosticText text = FindDiagnostic(report, CMakeError::InvalidBareArgument, "T:compile_definition");
        Check(CountText(text.actual, "has space") > 0, "id.cmake.validate.invalid_bare_argument.actual_text",
              "诊断的 actual 给出出错的取值文本；实际 [" + text.actual + "]");

        Project option_project("Proj");
        option_project.root().add_option(Argument::bare("OPT"), false, Argument::bare("has space"));
        CheckDiagnostic(validate(option_project), CMakeError::InvalidBareArgument, "option:OPT:description",
                        "id.cmake.validate.invalid_bare_argument.option_description");
    }

    void CaseVerbatimArgument() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "T", TargetType::StaticLibrary);
        target.add_compile_definition(Scope::Public, Argument::verbatim("PRIVATE"));
        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::VerbatimArgument, "T:compile_definition",
                        "id.cmake.validate.verbatim_argument.entry_value");
        // 措辞依据：计划书 §5.2 只规定合法类别集合（`Bare`/`Quoted`/`QuotedExpand`）与 `Verbatim` 的非法位置，
        // §5.8 只要求诊断给出对象、期望、实际与下一步动作，未冻结 `expected` 的措辞。下面这条按实现现状
        // 匹配其中的类别名，属措辞级断言：措辞调整不算契约变更，契约判定由上面的类别 + subject 承担。
        const DiagnosticText text = FindDiagnostic(report, CMakeError::VerbatimArgument, "T:compile_definition");
        Check(CountText(text.expected, "Bare") > 0, "id.cmake.validate.verbatim_argument.expected_text",
              "诊断的 expected 说明该位置只接受 Bare / Quoted / QuotedExpand；实际 [" + text.expected + "]");
    }

    // ==================== 目标类型与设置项类 ====================

    void CaseInvalidTargetType() noexcept
    {
        Project project("Proj");
        project.root().create_target("NoneTarget", TargetType::None);
        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::InvalidTargetType, "NoneTarget", "id.cmake.validate.invalid_target_type");
        Check(report.diagnostics.size() == 1, "id.cmake.validate.invalid_target_type.no_noise",
              "哨兵类型在类型判定后直接返回，不再报缺源目录等噪声；实际 " + std::to_string(report.diagnostics.size()) +
                  " 条：" + DescribeReport(report));
    }

    void CaseUnsupportedCompileStandard() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "T", TargetType::StaticLibrary);
        target.set_compile_standard(Argument::bare("cxx_std_17"));
        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::UnsupportedCompileStandard, "T", "id.cmake.validate.unsupported_compile_standard");
        Check(CountDiagnostics(report, CMakeError::InvalidBareArgument) == 0,
              "id.cmake.validate.unsupported_compile_standard.no_bare_noise",
              "cxx_std_17 是合法裸值，不应被裸值字符集判定误报");
        const DiagnosticText text = FindDiagnostic(report, CMakeError::UnsupportedCompileStandard, "T");
        Check(CountText(text.actual, "cxx_std_17") > 0, "id.cmake.validate.unsupported_compile_standard.actual_text",
              "诊断的 actual 给出收到的取值；实际 [" + text.actual + "]");
    }

    /**
     *  validate：收源目录形态判定（`InvalidSourceGlobDir`）
     *
     *  `include` 一类"不是 `src`"的相对目录**自 T78 起合法**（`docs/id/07` §7.4.4：`validate` 只判形态与
     *  越界，`InvalidSourceGlobDir` 只用于真实非法情形，"目录不是 `src`"不再构成非法）。因此非法样本改为
     *  `..`、盘符/绝对路径、空条目三类，并留一条 `include` 的阳性对照，避免把 T78 的收窄误判成回归。
     */
    void CaseInvalidSourceGlobDir() noexcept
    {
        Project    project("Proj");
        Directory& root = project.root();

        Target& empty_dir = root.create_target("Dir1", TargetType::StaticLibrary);
        empty_dir.set_source_glob_dir("");
        Target& parent_dir = root.create_target("Dir2", TargetType::StaticLibrary);
        parent_dir.set_source_glob_dir("../src");
        Target& drive_dir = root.create_target("Dir3", TargetType::StaticLibrary);
        drive_dir.set_source_glob_dir("C:/src");
        // 阳性对照：`include` 是合法相对目录（T78 起），不得报 `InvalidSourceGlobDir`
        Target& include_dir = root.create_target("Dir4", TargetType::StaticLibrary);
        include_dir.set_source_glob_dir("include");

        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::InvalidSourceGlobDir, "Dir1",
                        "id.cmake.validate.invalid_source_glob_dir.empty_entry");
        CheckDiagnostic(report, CMakeError::InvalidSourceGlobDir, "Dir2",
                        "id.cmake.validate.invalid_source_glob_dir.parent_segment");
        CheckDiagnostic(report, CMakeError::InvalidSourceGlobDir, "Dir3",
                        "id.cmake.validate.invalid_source_glob_dir.drive_letter");
        Check(CountDiagnostics(report, CMakeError::InvalidSourceGlobDir) == 3,
              "id.cmake.validate.invalid_source_glob_dir.count",
              "三个非法收源目录（空条目 / `..` / 盘符）各一条诊断，`include` 由 T78 起合法、不计入；实际 " +
                  std::to_string(CountDiagnostics(report, CMakeError::InvalidSourceGlobDir)) + " 条：" +
                  DescribeReport(report));
        Check(CountDiagnostics(report, CMakeError::InvalidSourceGlobDir, "Dir4") == 0,
              "id.cmake.validate.invalid_source_glob_dir.include_legal",
              "`include` 由 T78 起是合法相对目录（docs/id/07 §7.4.4）；对它报 InvalidSourceGlobDir 即回归");
        Check(CountDiagnostics(report, CMakeError::MissingSourceGlobDir) == 0,
              "id.cmake.validate.invalid_source_glob_dir.no_missing_noise",
              "调用过 set_source_glob_dir 时只报取值非法，不重复报缺失");
    }

    void CaseMissingSourceGlobDir() noexcept
    {
        Project project("Proj");
        project.root().create_target("T", TargetType::StaticLibrary);
        CheckDiagnostic(validate(project), CMakeError::MissingSourceGlobDir, "T", "id.cmake.validate.missing_source_glob_dir");

        // INTERFACE 库不写源通配：该类对它不适用
        Project interface_project("Proj");
        interface_project.root().create_target("Iface", TargetType::InterfaceLibrary);
        Check(CountDiagnostics(validate(interface_project), CMakeError::MissingSourceGlobDir) == 0,
              "id.cmake.validate.missing_source_glob_dir.not_interface", "INTERFACE 库不报缺源目录");

        // imported 目标不写第 5 项：该类对它不适用
        Project imported_project("Proj");
        Target& imported = imported_project.root().create_target("Imp", TargetType::StaticLibrary);
        imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.a"), std::nullopt, false);
        Check(CountDiagnostics(validate(imported_project), CMakeError::MissingSourceGlobDir) == 0,
              "id.cmake.validate.missing_source_glob_dir.not_imported", "imported 目标不报缺源目录");
    }

    void CaseInterfaceTargetScope() noexcept
    {
        Project project("Proj");
        project.add_external_target(Argument::bare("IDLog"));
        Target& interface_target = project.root().create_target("Iface", TargetType::InterfaceLibrary);
        interface_target.add_link_library(Scope::Private, Argument::bare("IDLog"));

        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::InterfaceTargetScope, "Iface:link_library",
                        "id.cmake.validate.interface_target_scope");
        Check(CountDiagnostics(report, CMakeError::ImportTargetScope) == 0,
              "id.cmake.validate.interface_target_scope.not_import", "非 imported 目标不报 ImportTargetScope（两类二选一）");
    }

    void CaseInterfaceTargetSetting() noexcept
    {
        Project project("Proj");
        Target& interface_target = project.root().create_target("Iface", TargetType::InterfaceLibrary);
        interface_target.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib"));
        interface_target.set_compile_standard(Argument::bare("cxx_std_20"));
        interface_target.set_source_glob_dir("src");

        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::InterfaceTargetSetting, "Iface", "id.cmake.validate.interface_target_setting");
        Check(CountDiagnostics(report, CMakeError::InterfaceTargetSetting) == 3,
              "id.cmake.validate.interface_target_setting.three_settings",
              "输出目录、编译特性、收源三项各一条诊断；实际 " +
                  std::to_string(CountDiagnostics(report, CMakeError::InterfaceTargetSetting)) + " 条");
    }

    void CaseInvalidFormOptionTarget() noexcept
    {
        // (a) 类型不支持切换形态
        Project executable_project("Proj");
        Target& app = CreateSourceTarget(executable_project, "App", TargetType::Executable);
        app.set_form_option(Argument::bare("APP_SHARED"), Argument::bare("APP_SHARED_BUILD"),
                            Argument::bare("APP_STATIC_BUILD"));
        const ID::CMake::ValidationReport type_report = validate(executable_project);
        CheckDiagnostic(type_report, CMakeError::InvalidFormOptionTarget, "App",
                        "id.cmake.validate.invalid_form_option_target.type");

        // (b) option 未在同目录声明：生成的 if(<未定义变量>) 恒假 → 静默走静态分支，同一类别
        Project undeclared_project("Proj");
        Target& module = CreateSourceTarget(undeclared_project, "Mod", TargetType::StaticLibrary);
        module.set_form_option(Argument::bare("MOD_SHARED"), Argument::bare("MOD_SHARED_BUILD"),
                               Argument::bare("MOD_STATIC_BUILD"));
        const ID::CMake::ValidationReport undeclared_report = validate(undeclared_project);
        CheckDiagnostic(undeclared_report, CMakeError::InvalidFormOptionTarget, "Mod",
                        "id.cmake.validate.invalid_form_option_target.undeclared_option");
        // 措辞依据：计划书 §5.4 / §5.8 只规定两种触发（类型不支持、option 未在同目录声明）归入同一类别，
        // 并要求诊断给出对象、期望、实际与下一步动作；`actual` 的具体措辞未冻结。下面这条按实现现状
        // 匹配该措辞，属附加定位信息（把「未声明」与「类型不支持」两种触发区分开），不是契约取值。
        const DiagnosticText undeclared_text =
            FindDiagnostic(undeclared_report, CMakeError::InvalidFormOptionTarget, "Mod");
        Check(CountText(undeclared_text.actual, "同目录未声明 option") > 0,
              "id.cmake.validate.invalid_form_option_target.undeclared_option_text",
              "诊断的 actual 指出未声明的 option 名；实际 [" + undeclared_text.actual + "]");

        // (c) 判序：INTERFACE 库上同时误用形态选项与输出目录 → 只报 InvalidFormOptionTarget
        Project ordering_project("Proj");
        Target& interface_target = ordering_project.root().create_target("Iface", TargetType::InterfaceLibrary);
        interface_target.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib"));
        interface_target.set_form_option(Argument::bare("IF_SHARED"), Argument::bare("IF_SHARED_BUILD"),
                                         Argument::bare("IF_STATIC_BUILD"));
        const ID::CMake::ValidationReport ordering_report = validate(ordering_project);
        Check(CountDiagnostics(ordering_report, CMakeError::InvalidFormOptionTarget, "Iface") == 1,
              "id.cmake.validate.invalid_form_option_target.priority",
              "INTERFACE 库上的形态选项报 InvalidFormOptionTarget；实际：" + DescribeReport(ordering_report));
        Check(CountDiagnostics(ordering_report, CMakeError::InterfaceTargetSetting, "Iface") == 0,
              "id.cmake.validate.invalid_form_option_target.suppresses_interface_setting",
              "两类只报一条（InvalidFormOptionTarget 优先），不再是 InterfaceTargetSetting");
    }

    // ==================== imported 目标类 ====================

    void CaseMissingImportLocation() noexcept
    {
        Project project("Proj");
        Target& imported = project.root().create_target("Imp", TargetType::StaticLibrary);
        imported.set_import(std::nullopt, std::nullopt, false);
        CheckDiagnostic(validate(project), CMakeError::MissingImportLocation, "Imp",
                        "id.cmake.validate.missing_import_location");
    }

    void CaseImportTargetSetting() noexcept
    {
        // imported 目标调用 set_output_directory
        Project output_project("Proj");
        Target& output_imported = output_project.root().create_target("ImpOut", TargetType::SharedLibrary);
        output_imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.dll"), std::nullopt, false);
        output_imported.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin"));
        CheckDiagnostic(validate(output_project), CMakeError::ImportTargetSetting, "ImpOut",
                        "id.cmake.validate.import_target_setting.output_directory");

        // INTERFACE imported 给出 location
        Project interface_project("Proj");
        Target& interface_imported = interface_project.root().create_target("ImpI", TargetType::InterfaceLibrary);
        interface_imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.dll"), std::nullopt, false);
        CheckDiagnostic(validate(interface_project), CMakeError::ImportTargetSetting, "ImpI",
                        "id.cmake.validate.import_target_setting.interface_location");

        // 非 SharedLibrary 给出 implib
        Project implib_project("Proj");
        Target& static_imported = implib_project.root().create_target("ImpS", TargetType::StaticLibrary);
        static_imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.a"),
                                   Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib/libImp.lib"), false);
        CheckDiagnostic(validate(implib_project), CMakeError::ImportTargetSetting, "ImpS",
                        "id.cmake.validate.import_target_setting.implib_only_shared");
    }

    void CaseImportTargetScope() noexcept
    {
        Project project("Proj");
        Target& imported = project.root().create_target("ImpE", TargetType::Executable);
        imported.set_import(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin/imp.exe"), std::nullopt, false);
        imported.add_include_directory(Scope::Public, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));

        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::ImportTargetScope, "ImpE:include_directory",
                        "id.cmake.validate.import_target_scope");
        Check(CountDiagnostics(report, CMakeError::InterfaceTargetScope) == 0,
              "id.cmake.validate.import_target_scope.not_interface_scope",
              "imported 目标只报 ImportTargetScope，不同时报 InterfaceTargetScope");

        // INTERFACE 库且 imported：同样只报 ImportTargetScope（二选一）
        Project interface_project("Proj");
        Target& interface_imported = interface_project.root().create_target("ImpI", TargetType::InterfaceLibrary);
        interface_imported.set_import(std::nullopt, std::nullopt, false);
        interface_imported.add_include_directory(Scope::Private, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));
        const ID::CMake::ValidationReport interface_report = validate(interface_project);
        CheckDiagnostic(interface_report, CMakeError::ImportTargetScope, "ImpI:include_directory",
                        "id.cmake.validate.import_target_scope.interface_imported");
        Check(CountDiagnostics(interface_report, CMakeError::InterfaceTargetScope) == 0,
              "id.cmake.validate.import_target_scope.interface_imported_no_duplicate",
              "INTERFACE 库 + imported 只报 ImportTargetScope");
    }

    // ==================== 唯一性与派生变量类 ====================

    /**
     *  DuplicateTarget：两处声明都可定位。
     *
     *  计划书原文与实现取值的差异（本用例据实现现状取证，不改实现、不改计划书）：
     *  - 计划书 §5.4（「`location` 给出两处（目录相对路径 + 目标名）」）与 §5.8 的诊断类别表
     *    （「`location` 给出两处」）要求两处位置都放进 `location`；
     *  - 实现现状把两处拆到两个字段：`location` 只放后出现的那一处，首次声明位置放在 `expected`
     *    （`目标名在全工程唯一，首次声明于 .:target:Dup`），`actual` 再给出重复的那一处。
     *  因此本用例断言「两处都可定位」——`location` 含第二处、`expected` 含第一处；两处文本都存在，
     *  只是字段分工与计划书不同。该差异已作为待同步文档项上报（不属可在测试侧修掉的缺陷）。
     */
    void CaseDuplicateTarget() noexcept
    {
        Project project("Proj");
        CreateSourceTarget(project, "Dup", TargetType::StaticLibrary);
        Directory& child = project.root().create_subdirectory("Sub");
        Target&    child_target = child.create_target("Dup", TargetType::StaticLibrary);
        child_target.set_source_glob_dir("src");

        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::DuplicateTarget, "Dup", "id.cmake.validate.duplicate_target");
        const DiagnosticText text = FindDiagnostic(report, CMakeError::DuplicateTarget, "Dup");
        Check(text.location == "Sub:target:Dup", "id.cmake.validate.duplicate_target.location",
              "第二处（后出现的声明）可定位；实际 [" + text.location + "]");
        Check(CountText(text.expected, ".:target:Dup") > 0, "id.cmake.validate.duplicate_target.expected_text",
              "第一处（首次声明）可定位；实际 [" + text.expected + "]");
    }

    void CaseDuplicateSubdirectory() noexcept
    {
        Project    project("Proj");
        Directory& root = project.root();
        root.create_subdirectory("A");
        root.create_subdirectory("A");
        root.create_subdirectory("B");
        root.create_referenced_subdirectory("B");

        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::DuplicateSubdirectory, "subdirectory:A",
                        "id.cmake.validate.duplicate_subdirectory.created");
        CheckDiagnostic(report, CMakeError::DuplicateSubdirectory, "subdirectory:B",
                        "id.cmake.validate.duplicate_subdirectory.mixed_entries");
        Check(CountDiagnostics(report, CMakeError::DuplicateSubdirectory) == 2,
              "id.cmake.validate.duplicate_subdirectory.count",
              "两种入口合并判定：恰好 2 条（A 与 B 各一条）");
    }

    void CaseDuplicateDerivedVariable() noexcept
    {
        // 目标的派生名与同目录 Variable 名冲突
        Project    project("Proj");
        Directory& root = project.root();
        root.add_variable(Argument::bare("T_SRC_FILES"), {Argument::bare("x")}, false, Argument::quoted(""));
        CreateSourceTarget(project, "T", TargetType::StaticLibrary);
        const ID::CMake::ValidationReport report = validate(project);
        CheckDiagnostic(report, CMakeError::DuplicateDerivedVariable, "T:T_SRC_FILES",
                        "id.cmake.validate.duplicate_derived_variable.variable");
        // 措辞依据：计划书 §5.4 / §5.8 只规定判定口径（派生名与 `Variable` 名或其它派生名按大写折叠相同）
        // 与诊断四要素，未冻结 `actual` 的措辞。下面这条按实现现状匹配它，用于证明诊断能区分「与 set 变量
        // 冲突」和「与其它目标的派生名冲突」这两种来源，属措辞级断言而非契约取值。
        const DiagnosticText text = FindDiagnostic(report, CMakeError::DuplicateDerivedVariable, "T:T_SRC_FILES");
        Check(CountText(text.actual, "set 变量 T_SRC_FILES") > 0, "id.cmake.validate.duplicate_derived_variable.actual_text",
              "actual 指出冲突来源（set 变量）；实际 [" + text.actual + "]");

        // 两个目标的派生名按大写折叠冲突（目标名本身不同，因此不是 DuplicateTarget）
        Project folded("Proj");
        CreateSourceTarget(folded, "Foo", TargetType::StaticLibrary);
        CreateSourceTarget(folded, "FOO", TargetType::StaticLibrary);
        const ID::CMake::ValidationReport folded_report = validate(folded);
        CheckDiagnostic(folded_report, CMakeError::DuplicateDerivedVariable, "FOO:FOO_SRC_FILES",
                        "id.cmake.validate.duplicate_derived_variable.folded_targets");
        Check(CountDiagnostics(folded_report, CMakeError::DuplicateTarget) == 0,
              "id.cmake.validate.duplicate_derived_variable.not_duplicate_target",
              "Foo 与 FOO 是不同目标名，按名称唯一性判定不重复");
    }

    // ==================== 链接判定类 ====================

    void CaseLinkTargetNotFound() noexcept
    {
        Project project("Proj");
        Target& target = CreateSourceTarget(project, "T", TargetType::StaticLibrary);
        target.add_link_library(Scope::Private, Argument::bare("NoSuchTarget"));
        CheckDiagnostic(validate(project), CMakeError::LinkTargetNotFound, "T:NoSuchTarget",
                        "id.cmake.validate.link_target_not_found");

        // 正例：已声明的目标名与登记的外部目标都能解析
        Project resolved("Proj");
        resolved.add_external_target(Argument::bare("IDLog"));
        CreateSourceTarget(resolved, "First", TargetType::StaticLibrary);
        Target& second = CreateSourceTarget(resolved, "Second", TargetType::StaticLibrary);
        second.add_link_library(Scope::Private, Argument::bare("First"));
        second.add_link_library(Scope::Private, Argument::bare("IDLog"));
        const ID::CMake::ValidationReport resolved_report = validate(resolved);
        Check(CountDiagnostics(resolved_report, CMakeError::LinkTargetNotFound) == 0,
              "id.cmake.validate.link_target_not_found.resolved_names",
              "本工程已声明目标名与外部目标登记都不报该类；实际：" + DescribeReport(resolved_report));
        Check(resolved_report.is_valid, "id.cmake.validate.link_target_not_found.resolved_valid",
              "两个目标都合法建模时整体校验通过；实际：" + DescribeReport(resolved_report));
    }

    // ==================== 类别名文本 ====================

    /// @brief 24 类类别名文本（计划书 §5.9 的 `failed:<类别>` 依赖它）与域外取值的映射
    void CaseErrorNameTexts() noexcept
    {
        struct ErrorName
        {
            CMakeError      code;
            std::string_view text;
        };
        const ErrorName names[] = {
            {CMakeError::EmptyName, "EmptyName"},
            {CMakeError::InvalidName, "InvalidName"},
            {CMakeError::InvalidBareArgument, "InvalidBareArgument"},
            {CMakeError::InvalidTargetType, "InvalidTargetType"},
            {CMakeError::UnsupportedCompileStandard, "UnsupportedCompileStandard"},
            {CMakeError::InvalidSourceGlobDir, "InvalidSourceGlobDir"},
            {CMakeError::DuplicateTarget, "DuplicateTarget"},
            {CMakeError::DuplicateSubdirectory, "DuplicateSubdirectory"},
            {CMakeError::DuplicateDerivedVariable, "DuplicateDerivedVariable"},
            {CMakeError::MissingSourceGlobDir, "MissingSourceGlobDir"},
            {CMakeError::InterfaceTargetScope, "InterfaceTargetScope"},
            {CMakeError::InterfaceTargetSetting, "InterfaceTargetSetting"},
            {CMakeError::InvalidFormOptionTarget, "InvalidFormOptionTarget"},
            {CMakeError::MissingImportLocation, "MissingImportLocation"},
            {CMakeError::ImportTargetSetting, "ImportTargetSetting"},
            {CMakeError::ImportTargetScope, "ImportTargetScope"},
            {CMakeError::VerbatimArgument, "VerbatimArgument"},
            {CMakeError::LinkTargetNotFound, "LinkTargetNotFound"},
            {CMakeError::PathEscapesProjectRoot, "PathEscapesProjectRoot"},
            {CMakeError::TargetModified, "TargetModified"},
            {CMakeError::MissingFile, "MissingFile"},
            {CMakeError::MissingMarker, "MissingMarker"},
            {CMakeError::AmbiguousMarker, "AmbiguousMarker"},
            {CMakeError::IoFailed, "IoFailed"},
        };
        for (const ErrorName& item : names)
        {
            Check(ID::CMake::to_string(item.code) == item.text,
                  std::string("id.cmake.validate.error_name.") + std::string(item.text),
                  "类别名与枚举名一致（CLI 的 failed:<类别> 依赖该文本）");
        }
        // 依据：计划书 §5.8 的类别表是闭集（24 类，无哨兵、无成功值），§5.9 只要求 `failed:<类别>` 取该表
        // 的类别名；域外取值（用 static_cast 造出）映射成什么文本，计划书未规定。下面这条按实现现状断言
        // "不静默归到某个具体类别"，即只证明兜底行为不会伪造诊断，不冻结兜底文本本身。
        Check(ID::CMake::to_string(static_cast<CMakeError>(200)) == "Unknown", "id.cmake.validate.error_name.unknown",
              "域外取值映射为 Unknown，不静默归到某个具体类别");
    }
} // namespace

namespace ID::Bench
{
    void RunCmakeValidateCases() noexcept
    {
        CaseValidProject();
        CaseEmptyName();
        CaseInvalidName();
        CaseInvalidBareArgument();
        CaseVerbatimArgument();
        CaseInvalidTargetType();
        CaseUnsupportedCompileStandard();
        CaseInvalidSourceGlobDir();
        CaseMissingSourceGlobDir();
        CaseInterfaceTargetScope();
        CaseInterfaceTargetSetting();
        CaseInvalidFormOptionTarget();
        CaseMissingImportLocation();
        CaseImportTargetSetting();
        CaseImportTargetScope();
        CaseDuplicateTarget();
        CaseDuplicateSubdirectory();
        CaseDuplicateDerivedVariable();
        CaseLinkTargetNotFound();
        CaseErrorNameTexts();
    }
} // namespace ID::Bench
