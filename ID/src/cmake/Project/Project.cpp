#include "cmake/Project/Project.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/Common/Assert.hpp"

namespace ID::CMake
{
    namespace
    {
        // ---------- 名字与参数形态 ----------

        bool is_ascii_letter(char character) noexcept
        {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        bool is_ascii_digit(char character) noexcept
        {
            return character >= '0' && character <= '9';
        }

        // CMake 标识符形态：首字符必须是字母，其余字符只能是字母、数字或下划线
        bool is_valid_name(std::string_view name) noexcept
        {
            if (name.empty())
            {
                return false;
            }
            if (!is_ascii_letter(name.front()))
            {
                return false;
            }
            for (const char character : name)
            {
                if (!is_ascii_letter(character) && !is_ascii_digit(character) && character != '_')
                {
                    return false;
                }
            }
            return true;
        }

        // 绝对路径形态：前导分隔符，或 "<字母>:" 的盘符形式（Windows 与 POSIX 两种写法都在此拒绝）
        bool is_absolute_like(std::string_view path) noexcept
        {
            if (path.empty())
            {
                return false;
            }
            if (path.front() == '/' || path.front() == '\\')
            {
                return true;
            }
            return path.size() >= 2 && is_ascii_letter(path.front()) && path[1] == ':';
        }

        // 收源目录的形态判定：相对插件根、非空、不只含分隔符、不含任何 .. 段。
        // 拒绝全部 .. 段即同时挡住"越出插件根"；"条目落在清单 source_dirs 声明的范围内"由装配层保证，
        // 这里不读清单、也不据 kSourceGlobDirectory 限制目录名（清单声明 src 之外的范围属合法）
        bool is_valid_source_glob_dir(std::string_view path) noexcept
        {
            if (path.empty() || is_absolute_like(path))
            {
                return false;
            }

            bool        has_segment = false;
            std::size_t begin       = 0;
            for (;;)
            {
                const std::size_t end = path.find_first_of("/\\", begin);
                const std::string_view segment =
                    path.substr(begin, (end == std::string_view::npos ? path.size() : end) - begin);
                if (segment == "..")
                {
                    return false;
                }
                if (!segment.empty() && segment != ".")
                {
                    has_segment = true;
                }
                if (end == std::string_view::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return has_segment;
        }

        // 条目类别在定位文本里的名字，便于失败后定位到具体条目
        std::string_view entry_kind_name(TargetEntryKind kind) noexcept
        {
            switch (kind)
            {
                case TargetEntryKind::CompileDefinition: return "compile_definition";
                case TargetEntryKind::IncludeDirectory:  return "include_directory";
                case TargetEntryKind::LinkDirectory:     return "link_directory";
                case TargetEntryKind::LinkLibrary:       return "link_library";
                case TargetEntryKind::Count:             break;
            }
            return "unknown";
        }

        // 参数片段类别名：诊断里要区分"不是 Bare"，因此需要可读的类别文本
        std::string_view argument_kind_name(ArgumentKind kind) noexcept
        {
            switch (kind)
            {
                case ArgumentKind::Bare:         return "Bare";
                case ArgumentKind::Quoted:       return "Quoted";
                case ArgumentKind::QuotedExpand: return "QuotedExpand";
                case ArgumentKind::Verbatim:     return "Verbatim";
            }
            return "Unknown";
        }

        // 分支载荷类别名：用于 InvalidMacroBranch 的实际取值
        std::string_view branch_payload_name(BranchPayload payload) noexcept
        {
            switch (payload)
            {
                case BranchPayload::CompileDefinition: return "CompileDefinition";
                case BranchPayload::Variable:          return "Variable";
            }
            return "Unknown";
        }

        // ---------- 定位文本 ----------

        std::string make_target_location(const Directory& directory, const Target& target)
        {
            std::string location(directory.get_relative_path());
            location.append(":target:").append(target.get_name());
            return location;
        }

        // ---------- 诊断构造（只构造，不改报告） ----------

        std::optional<Diagnostic> make_name_diagnostic(std::string_view name, const std::string& subject, const std::string& location)
        {
            if (name.empty())
            {
                return Diagnostic{CMakeError::EmptyName, subject, "非空名称", "空串", location,
                                  "为该对象起一个非空名称后重跑 validate"};
            }
            if (!is_valid_name(name))
            {
                return Diagnostic{CMakeError::InvalidName, subject,
                                  "首字符为字母、其余字符为字母/数字/下划线的 CMake 标识符", std::string(name), location,
                                  "修改该名称，去掉标识符不允许的字符后重跑 validate"};
            }
            return std::nullopt;
        }

        std::optional<Diagnostic> make_bare_diagnostic(const Argument& argument, const std::string& subject, const std::string& location)
        {
            if (is_valid_bare_argument(argument))
            {
                return std::nullopt;
            }
            return Diagnostic{CMakeError::InvalidBareArgument, subject,
                              "非空且只含 A-Z a-z 0-9 _ - . / : + = @ , 的裸值，或改用 Quoted / QuotedExpand",
                              "裸值片段：" + std::string(argument.get_text()), location,
                              "修正该取值的字符集，或把它改成带引号的片段后重跑 validate"};
        }

        std::optional<Diagnostic> make_verbatim_diagnostic(const Argument& argument, const std::string& subject, const std::string& location)
        {
            if (argument.get_kind() != ArgumentKind::Verbatim)
            {
                return std::nullopt;
            }
            return Diagnostic{CMakeError::VerbatimArgument, subject, "Bare / Quoted / QuotedExpand 的参数取值",
                              "Verbatim 片段：" + std::string(argument.get_text()), location,
                              "把该取值改成带引号的片段后重跑 validate；Verbatim 只能用于条件、配置块命令与尾部命令"};
        }

        // 待检查的参数片段：指向模型内稳定成员，subject 只用于诊断定位
        struct ArgumentCheck
        {
            const Argument* argument = nullptr;
            std::string     subject  = {};
        };

        // ---------- 遍历 ----------

        std::size_t count_directories(const Directory& directory) noexcept
        {
            std::size_t result = 1;
            for (std::size_t index = 0; index < directory.subdirectory_entry_count(); ++index)
            {
                const Directory* child = directory.subdirectory_at(index);
                if (child != nullptr)
                {
                    result += count_directories(*child);
                }
            }
            return result;
        }

        // 深度优先（父先于子）定位第 target_index 个目录；cursor 是遍历进度，跨递归共享
        const Directory* directory_by_index(const Directory& directory, std::size_t& cursor, std::size_t target_index) noexcept
        {
            if (cursor == target_index)
            {
                return &directory;
            }
            ++cursor;

            for (std::size_t index = 0; index < directory.subdirectory_entry_count(); ++index)
            {
                const Directory* child = directory.subdirectory_at(index);
                if (child == nullptr)
                {
                    continue;
                }

                const Directory* found = directory_by_index(*child, cursor, target_index);
                if (found != nullptr)
                {
                    return found;
                }
            }
            return nullptr;
        }

        // 同目录是否声明过该 option：FormOption::option 必须引用它，否则生成的 if(<未定义变量>) 恒假
        bool has_declared_option(const Directory& directory, const Argument& option_name) noexcept
        {
            for (std::size_t index = 0; index < directory.option_count(); ++index)
            {
                if (directory.option_at(index).name.get_text() == option_name.get_text())
                {
                    return true;
                }
            }
            return false;
        }

        // ---------- 守卫校验 ----------

        // 守卫的形态判定：被守卫的目标名必须非空 Bare，说明串不含 CR/LF（形态固定为拒绝）。
        // 目标块守卫与子目录接入守卫共用本判定，触发都归 InvalidGuard
        void validate_guard(ValidationReport& report, const TargetGuard& guard, const std::string& subject, const std::string& location)
        {
            if (guard.target.get_text().empty() || guard.target.get_kind() != ArgumentKind::Bare)
            {
                report.diagnostics.push_back(
                    Diagnostic{CMakeError::InvalidGuard, subject, "被守卫的目标名是非空 Bare 片段",
                               "取值类别：" + std::string(argument_kind_name(guard.target.get_kind())) + "，文本：\"" +
                                   std::string(guard.target.get_text()) + "\"",
                               location + ":guard.target",
                               "把守卫的目标名改成非空 Bare 片段（如 Argument::bare(\"Example\")）后重跑 validate"});
                report.is_valid = false;
            }
            if (guard.message.get_text().find('\r') != std::string_view::npos
                || guard.message.get_text().find('\n') != std::string_view::npos)
            {
                report.diagnostics.push_back(Diagnostic{CMakeError::InvalidGuard, subject,
                                                       "守卫说明串不含 CR / LF（单行）", "说明串含换行字符",
                                                       location + ":guard.message",
                                                       "把守卫的说明串压成单行后重跑 validate"});
                report.is_valid = false;
            }
        }

        // ---------- 目标校验 ----------

        void validate_target(ValidationReport& report, const Directory& directory, const Target& target)
        {
            const std::string location = make_target_location(directory, target);
            const std::string subject(target.get_name());
            const TargetType  type         = target.get_type();
            const bool        is_imported  = target.has_import();
            const bool        is_interface = type == TargetType::InterfaceLibrary;

            // InvalidTargetType 先判：哨兵类型没有可建模的语句形态，其余判定对它只会产生噪声
            if (type == TargetType::None)
            {
                report.diagnostics.push_back(Diagnostic{CMakeError::InvalidTargetType, subject,
                                                       "Executable / StaticLibrary / SharedLibrary / InterfaceLibrary 之一",
                                                       "None（哨兵）", location,
                                                       "在 create_target 调用处给出真实目标类型，或删除该目标后重跑 validate"});
                report.is_valid = false;
                return;
            }

            if (const std::optional<Diagnostic> diagnostic = make_name_diagnostic(target.get_name(), subject, location);
                diagnostic.has_value())
            {
                report.diagnostics.push_back(*diagnostic);
                report.is_valid = false;
            }

            // 条目：参数形态（Verbatim 位置、裸值字符集、条件裸值）与作用域
            for (std::size_t kind_index = 0; kind_index < to_target_entry_kind_uint(TargetEntryKind::Count); ++kind_index)
            {
                const TargetEntryKind kind  = to_target_entry_kind(static_cast<std::uint8_t>(kind_index));
                const std::string     kind_name(entry_kind_name(kind));
                const std::size_t     count = target.entry_count(kind);

                for (std::size_t index = 0; index < count; ++index)
                {
                    const ScopedEntry& entry = target.entry_at(kind, index);

                    std::string entry_location(location);
                    entry_location.append(":").append(kind_name).append(":").append(std::to_string(index));

                    const std::string entry_subject = subject + ":" + kind_name;

                    if (const std::optional<Diagnostic> diagnostic = make_verbatim_diagnostic(entry.value, entry_subject, entry_location);
                        diagnostic.has_value())
                    {
                        report.diagnostics.push_back(*diagnostic);
                        report.is_valid = false;
                    }
                    if (const std::optional<Diagnostic> diagnostic = make_bare_diagnostic(entry.value, entry_subject, entry_location);
                        diagnostic.has_value())
                    {
                        report.diagnostics.push_back(*diagnostic);
                        report.is_valid = false;
                    }
                    if (entry.condition.has_value())
                    {
                        if (const std::optional<Diagnostic> diagnostic =
                                make_bare_diagnostic(*entry.condition, entry_subject + ":condition", entry_location);
                            diagnostic.has_value())
                        {
                            report.diagnostics.push_back(*diagnostic);
                            report.is_valid = false;
                        }
                    }

                    // 作用域：imported 优先于 INTERFACE 库（两者二选一，不重复报）
                    if (entry.scope != Scope::Interface)
                    {
                        if (is_imported)
                        {
                            report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetScope, entry_subject,
                                                                   "imported 目标的条目作用域为 INTERFACE",
                                                                   std::string(scope_keyword(entry.scope)), entry_location,
                                                                   "把该条目的作用域改成 Scope::Interface 后重跑 validate"});
                            report.is_valid = false;
                        }
                        else if (is_interface)
                        {
                            report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetScope, entry_subject,
                                                                   "INTERFACE 库的条目作用域为 INTERFACE",
                                                                   std::string(scope_keyword(entry.scope)), entry_location,
                                                                   "把该条目的作用域改成 Scope::Interface，或改用非 INTERFACE 目标后重跑 validate"});
                            report.is_valid = false;
                        }
                    }
                }
            }

            // 目标级参数片段：输出目录、编译特性、形态选项三项、import 两项
            std::vector<ArgumentCheck> checks;
            checks.push_back(ArgumentCheck{&target.get_output_directory(), subject + ":output_directory"});
            checks.push_back(ArgumentCheck{&target.get_compile_standard(), subject + ":compile_standard"});
            if (target.has_form_option())
            {
                const FormOption& form = target.get_form_option();
                checks.push_back(ArgumentCheck{&form.option, subject + ":form_option.option"});
                checks.push_back(ArgumentCheck{&form.shared_define, subject + ":form_option.shared_define"});
                checks.push_back(ArgumentCheck{&form.static_define, subject + ":form_option.static_define"});
            }
            if (is_imported)
            {
                const ImportSpec& spec = target.get_import();
                if (spec.location.has_value())
                {
                    checks.push_back(ArgumentCheck{&*spec.location, subject + ":import.location"});
                }
                if (spec.implib.has_value())
                {
                    checks.push_back(ArgumentCheck{&*spec.implib, subject + ":import.implib"});
                }
            }

            for (const ArgumentCheck& check : checks)
            {
                if (const std::optional<Diagnostic> diagnostic = make_bare_diagnostic(*check.argument, check.subject, location);
                    diagnostic.has_value())
                {
                    report.diagnostics.push_back(*diagnostic);
                    report.is_valid = false;
                }
            }

            if (is_imported)
            {
                const ImportSpec& spec = target.get_import();

                // 可执行与库目标必须有产物位置；INTERFACE 库不产生产物，location 必须为空
                if (!is_interface && !spec.location.has_value())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::MissingImportLocation, subject,
                                                           "IMPORTED_LOCATION 取值", "未提供 location", location,
                                                           "为该 imported 目标补 location，或把它改回源码目标后重跑 validate"});
                    report.is_valid = false;
                }

                // imported 目标不参与编译：收源、输出目录、编译特性、形态选项一律不适用
                if (target.has_source_glob_dir())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置收源目录",
                                                           "调用了 set_source_glob_dir(" + std::string(target.get_source_glob_dir()) + ")",
                                                           location, "移除该 imported 目标的 set_source_glob_dir 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_output_directory())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置输出目录",
                                                           "调用了 set_output_directory(" +
                                                               std::string(target.get_output_directory().get_text()) + ")",
                                                           location, "移除该 imported 目标的 set_output_directory 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_config_output_directories())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置按配置的输出目录",
                                                           "调用了 set_config_output_directories",
                                                           location,
                                                           "移除该 imported 目标的 set_config_output_directories 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_compile_standard())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置编译特性",
                                                           "调用了 set_compile_standard(" +
                                                               std::string(target.get_compile_standard().get_text()) + ")",
                                                           location, "移除该 imported 目标的 set_compile_standard 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_build_rpath())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置 BUILD_RPATH",
                                                           "调用了 set_build_rpath",
                                                           location, "移除该 imported 目标的 set_build_rpath 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_position_independent_code())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置 POSITION_INDEPENDENT_CODE",
                                                           "调用了 set_position_independent_code",
                                                           location,
                                                           "移除该 imported 目标的 set_position_independent_code 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_form_option())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "imported 目标不设置形态选项",
                                                           "调用了 set_form_option(" +
                                                               std::string(target.get_form_option().option.get_text()) + ")",
                                                           location, "移除该 imported 目标的 set_form_option 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (is_interface && (spec.location.has_value() || spec.implib.has_value()))
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "INTERFACE imported 目标的 location 与 implib 为空",
                                                           "给出了 location 或 implib", location,
                                                           "清空该 INTERFACE imported 目标的 location / implib 后重跑 validate"});
                    report.is_valid = false;
                }
                if (!is_interface && spec.implib.has_value() && type != TargetType::SharedLibrary)
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::ImportTargetSetting, subject,
                                                           "IMPORTED_IMPLIB 只允许 SharedLibrary 给出",
                                                           "非 SharedLibrary 的 imported 目标给出了 implib", location,
                                                           "去掉该 imported 目标的 implib，或把目标类型改为 SharedLibrary 后重跑 validate"});
                    report.is_valid = false;
                }
                return;
            }

            // 非 imported：形态选项先判（InvalidFormOptionTarget 优先于 InterfaceTargetSetting）
            bool has_form_error = false;
            if (target.has_form_option())
            {
                const bool is_form_eligible = type == TargetType::StaticLibrary || type == TargetType::SharedLibrary;
                if (!is_form_eligible)
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InvalidFormOptionTarget, subject,
                                                           "形态选项只用于 StaticLibrary 或 SharedLibrary",
                                                           "该目标类型不支持切换形态", location,
                                                           "移除该目标的 set_form_option 调用，或改用静态/共享库后重跑 validate"});
                    report.is_valid = false;
                    has_form_error = true;
                }
                else if (!has_declared_option(directory, target.get_form_option().option))
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InvalidFormOptionTarget, subject,
                                                           "形态选项引用同目录已声明的 Option",
                                                           "同目录未声明 option " +
                                                               std::string(target.get_form_option().option.get_text()),
                                                           location,
                                                           "在该目录补 add_option 声明，或把 if(<未定义变量>) 的选项名改成已声明的名字后重跑 validate"});
                    report.is_valid = false;
                    has_form_error = true;
                }
            }

            // INTERFACE 库不设输出目录、编译特性与收源目录；被 InvalidFormOptionTarget 压制时只报前者
            if (!has_form_error && is_interface)
            {
                if (target.has_output_directory())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetSetting, subject,
                                                           "INTERFACE 库不设置输出目录", "调用了 set_output_directory",
                                                           location, "移除该 INTERFACE 库的 set_output_directory 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_config_output_directories())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetSetting, subject,
                                                           "INTERFACE 库不设置按配置的输出目录",
                                                           "调用了 set_config_output_directories",
                                                           location,
                                                           "移除该 INTERFACE 库的 set_config_output_directories 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_compile_standard())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetSetting, subject,
                                                           "INTERFACE 库不设置编译特性", "调用了 set_compile_standard",
                                                           location, "移除该 INTERFACE 库的 set_compile_standard 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_build_rpath())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetSetting, subject,
                                                           "INTERFACE 库不设置 BUILD_RPATH", "调用了 set_build_rpath",
                                                           location, "移除该 INTERFACE 库的 set_build_rpath 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_position_independent_code())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetSetting, subject,
                                                           "INTERFACE 库不设置 POSITION_INDEPENDENT_CODE",
                                                           "调用了 set_position_independent_code",
                                                           location,
                                                           "移除该 INTERFACE 库的 set_position_independent_code 调用后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_source_glob_dir())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InterfaceTargetSetting, subject,
                                                           "INTERFACE 库不设置收源目录", "调用了 set_source_glob_dir",
                                                           location, "移除该 INTERFACE 库的 set_source_glob_dir 调用后重跑 validate"});
                    report.is_valid = false;
                }
            }

            // 非 INTERFACE 库必须收源：一条收源目录都没有才报 MissingSourceGlobDir
            if (!is_interface && target.source_glob_dir_count() == 0)
            {
                report.diagnostics.push_back(Diagnostic{CMakeError::MissingSourceGlobDir, subject,
                                                       "登记至少一条收源目录", "一条收源目录都没有", location,
                                                       "为该目标补 add_source_glob_dir(\"src\") 后重跑 validate"});
                report.is_valid = false;
            }

            // 每条收源目录只判形态与越界：绝对路径、含 .. 段、越出插件根、空条目或只含分隔符。
            // "目录不是 src" 不再构成非法：清单声明 src 之外的范围时由生成侧按声明逐项登记
            for (std::size_t index = 0; index < target.source_glob_dir_count(); ++index)
            {
                const std::string_view glob_dir = target.source_glob_dir_at(index);
                if (is_valid_source_glob_dir(glob_dir))
                {
                    continue;
                }

                std::string entry_location(location);
                entry_location.append(":source_glob_dir:").append(std::to_string(index));

                report.diagnostics.push_back(Diagnostic{CMakeError::InvalidSourceGlobDir, subject,
                                                       "相对插件根的目录（非空、不含 .. 段、不是绝对路径、不带盘符）",
                                                       std::string(glob_dir), entry_location,
                                                       "把该收源目录改成插件根内的相对目录后重跑 validate"});
                report.is_valid = false;
            }

            if (target.has_compile_standard() && target.get_compile_standard().get_text() != kSupportedCompileStandard)
            {
                report.diagnostics.push_back(Diagnostic{CMakeError::UnsupportedCompileStandard, subject,
                                                       std::string(kSupportedCompileStandard),
                                                       std::string(target.get_compile_standard().get_text()), location,
                                                       "把编译特性改成 cxx_std_20 后重跑 validate"});
                report.is_valid = false;
            }

            // 按配置输出目录：两个配置的取值都必须给出（只给一半按"为空"处理），且与单目录表达互斥
            if (target.has_config_output_directories())
            {
                const ConfigOutputDirectories& directories = target.get_config_output_directories();
                if (directories.debug.get_text().empty() || directories.release.get_text().empty())
                {
                    report.diagnostics.push_back(
                        Diagnostic{CMakeError::InvalidConfigOutput, subject,
                                   "debug 与 release 两个配置的输出目录都非空",
                                   "debug=\"" + std::string(directories.debug.get_text()) + "\"，release=\"" +
                                       std::string(directories.release.get_text()) + "\"",
                                   location,
                                   "为两个配置都给出输出目录（形如 ${ID_OUTPUT_ROOT}/debug 与 ${ID_OUTPUT_ROOT}/release）后重跑 validate"});
                    report.is_valid = false;
                }
                if (target.has_output_directory())
                {
                    report.diagnostics.push_back(
                        Diagnostic{CMakeError::InvalidConfigOutput, subject,
                                   "按配置输出目录与单目录输出目录互斥，同一目标只选其中一种表达",
                                   "同时调用了 set_output_directory 与 set_config_output_directories", location,
                                   "删除其中一种输出目录表达后重跑 validate"});
                    report.is_valid = false;
                }
            }

            // 目标守卫：形态固定为拒绝，因此目标名必须是非空 Bare，说明串不能含 CR/LF
            if (target.has_guard())
            {
                validate_guard(report, target.get_guard(), subject, location);
            }

            // 互斥分支块：条件非空、两分支载荷类别相同、两分支取值非空；取值与条件的参数形态另按位置判
            for (std::size_t index = 0; index < target.branch_count(); ++index)
            {
                const BranchBlock& branch          = target.branch_at(index);
                const std::string  branch_location = location + ":branch:" + std::to_string(index);

                if (branch.condition.get_text().empty())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InvalidMacroBranch, subject,
                                                           "分支条件非空", "条件为空串", branch_location + ":condition",
                                                           "为 add_branch 给出非空条件后重跑 validate"});
                    report.is_valid = false;
                }
                if (branch.then_statement.payload != branch.else_statement.payload)
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InvalidMacroBranch, subject,
                                                           "if 与 else 两个分支的载荷类别相同",
                                                           "if 分支：" + std::string(branch_payload_name(branch.then_statement.payload)) +
                                                               "，else 分支：" +
                                                               std::string(branch_payload_name(branch.else_statement.payload)),
                                                           branch_location,
                                                           "把两个分支改成同一载荷类别后重跑 validate"});
                    report.is_valid = false;
                }
                if (branch.then_statement.value.get_text().empty() || branch.else_statement.value.get_text().empty())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InvalidMacroBranch, subject,
                                                           "if 与 else 两个分支的取值都非空",
                                                           "if 分支取值长度 " +
                                                               std::to_string(branch.then_statement.value.get_text().size()) +
                                                               "，else 分支取值长度 " +
                                                               std::to_string(branch.else_statement.value.get_text().size()),
                                                           branch_location,
                                                           "为两个分支补上取值后重跑 validate"});
                    report.is_valid = false;
                }

                // 条件可以是原始词法表达式（Verbatim），因此只判裸值字符集
                if (const std::optional<Diagnostic> diagnostic =
                        make_bare_diagnostic(branch.condition, subject + ":branch.condition", branch_location);
                    diagnostic.has_value())
                {
                    report.diagnostics.push_back(*diagnostic);
                    report.is_valid = false;
                }

                const bool is_compile_definition = branch.then_statement.payload == BranchPayload::CompileDefinition;
                const Argument* values[2] = {&branch.then_statement.value, &branch.else_statement.value};
                const char*     branches[2] = {"then", "else"};
                for (std::size_t branch_index = 0; branch_index < 2; ++branch_index)
                {
                    const std::string branch_subject = subject + ":branch." + branches[branch_index];

                    if (const std::optional<Diagnostic> diagnostic =
                            make_bare_diagnostic(*values[branch_index], branch_subject, branch_location);
                        diagnostic.has_value())
                    {
                        report.diagnostics.push_back(*diagnostic);
                        report.is_valid = false;
                    }
                    if (!is_compile_definition)
                    {
                        // 变量载荷的取值承载 set(...) 的完整参数串，可按 Verbatim 给出，不做 Verbatim 位置判定
                        continue;
                    }
                    if (const std::optional<Diagnostic> diagnostic =
                            make_verbatim_diagnostic(*values[branch_index], branch_subject, branch_location);
                        diagnostic.has_value())
                    {
                        report.diagnostics.push_back(*diagnostic);
                        report.is_valid = false;
                    }
                }
            }

            // BUILD_RPATH：非空列表、每项非空串、且不含 $<CONFIG>（配置段只以字面量出现在属性里）
            if (target.has_build_rpath())
            {
                const std::vector<std::string>& directories = target.get_build_rpath();

                std::string reason;
                if (directories.empty())
                {
                    reason = "列表为空";
                }
                for (std::size_t index = 0; reason.empty() && index < directories.size(); ++index)
                {
                    if (directories[index].empty())
                    {
                        reason = "第 " + std::to_string(index) + " 项是空串";
                    }
                    else if (directories[index].find("$<CONFIG>") != std::string::npos)
                    {
                        reason = "第 " + std::to_string(index) + " 项含 $<CONFIG>：" + directories[index];
                    }
                }

                if (!reason.empty())
                {
                    report.diagnostics.push_back(Diagnostic{CMakeError::InvalidBuildRpath, subject,
                                                           "非空的字面目录列表，每项非空且不含 $<CONFIG>", reason,
                                                           location + ":build_rpath",
                                                           "修正 set_build_rpath 的取值（去掉空项与 $<CONFIG>）后重跑 validate"});
                    report.is_valid = false;
                }
            }
        }

        // ---------- 目录内容校验 ----------

        void validate_directory_content(ValidationReport& report, const Directory& directory)
        {
            const std::string location(directory.get_relative_path());

            // 子目录条目：空名、非法名、同目录重名（两种入口合并判定）
            for (std::size_t index = 0; index < directory.subdirectory_entry_count(); ++index)
            {
                const SubdirectoryEntry& entry   = directory.subdirectory_entry_at(index);
                const std::string        subject = "subdirectory:" + entry.name;

                // 带源目录表达式的条目按表达式写出，名称不是 CMake 标识符：只判表达式形态，不判目录名
                if (!entry.source.has_value())
                {
                    if (const std::optional<Diagnostic> diagnostic = make_name_diagnostic(entry.name, subject, location);
                        diagnostic.has_value())
                    {
                        report.diagnostics.push_back(*diagnostic);
                        report.is_valid = false;
                    }
                }
                else
                {
                    std::string entry_location(location);
                    entry_location.append(":subdirectory:").append(std::to_string(index));

                    const ArgumentCheck entry_checks[2] = {ArgumentCheck{&*entry.source, subject + ":source"},
                                                           ArgumentCheck{entry.binary.has_value() ? &*entry.binary : nullptr,
                                                                         subject + ":binary"}};
                    for (const ArgumentCheck& check : entry_checks)
                    {
                        if (check.argument == nullptr)
                        {
                            continue;   // 未给出的二进制目录不参与判定
                        }
                        if (const std::optional<Diagnostic> diagnostic =
                                make_bare_diagnostic(*check.argument, check.subject, entry_location);
                            diagnostic.has_value())
                        {
                            report.diagnostics.push_back(*diagnostic);
                            report.is_valid = false;
                        }
                        if (const std::optional<Diagnostic> diagnostic =
                                make_verbatim_diagnostic(*check.argument, check.subject, entry_location);
                            diagnostic.has_value())
                        {
                            report.diagnostics.push_back(*diagnostic);
                            report.is_valid = false;
                        }
                    }
                }

                // 接入前的守卫与目标守卫同一套形态判定（InvalidGuard）
                if (entry.guard.has_value())
                {
                    validate_guard(report, *entry.guard, subject, location);
                }

                for (std::size_t earlier = 0; earlier < index; ++earlier)
                {
                    if (directory.subdirectory_entry_at(earlier).name == entry.name)
                    {
                        report.diagnostics.push_back(Diagnostic{CMakeError::DuplicateSubdirectory, subject,
                                                               "同一目录内子目录名唯一",
                                                               "第 " + std::to_string(index) + " 条与第 " +
                                                                   std::to_string(earlier) + " 条同名",
                                                               location,
                                                               "删除重复的 add_subdirectory 条目或改名后重跑 validate"});
                        report.is_valid = false;
                        break;
                    }
                }
            }

            // option、变量、全局编译选项与语言的参数片段
            std::vector<ArgumentCheck> checks;
            for (std::size_t index = 0; index < directory.option_count(); ++index)
            {
                const Option& option = directory.option_at(index);
                checks.push_back(ArgumentCheck{&option.name, "option:" + std::string(option.name.get_text()) + ":name"});
                checks.push_back(ArgumentCheck{&option.description,
                                               "option:" + std::string(option.name.get_text()) + ":description"});
            }
            for (std::size_t index = 0; index < directory.variable_count(); ++index)
            {
                const Variable& variable = directory.variable_at(index);
                const std::string prefix = "set:" + std::string(variable.name.get_text());
                checks.push_back(ArgumentCheck{&variable.name, prefix + ":name"});
                checks.push_back(ArgumentCheck{&variable.doc_string, prefix + ":doc_string"});
                for (std::size_t value_index = 0; value_index < variable.values.size(); ++value_index)
                {
                    checks.push_back(ArgumentCheck{&variable.values[value_index], prefix + ":value:" + std::to_string(value_index)});
                }
            }
            // 配置段"守卫 + 定义"条目：名字、取值、守卫条件与说明串的参数形态。
            // 条件可为原始词法表达式（Verbatim）；取值与说明串是参数位置，Verbatim 由 make_verbatim_diagnostic 判
            for (std::size_t index = 0; index < directory.configuration_variable_count(); ++index)
            {
                const ConfigurationVariable& variable = directory.configuration_variable_at(index);
                const std::string            prefix   = "configuration_variable:" + std::string(variable.name.get_text());

                checks.push_back(ArgumentCheck{&variable.name, prefix + ":name"});
                checks.push_back(ArgumentCheck{&variable.value, prefix + ":value"});
                checks.push_back(ArgumentCheck{&variable.guard_condition, prefix + ":guard_condition"});
                checks.push_back(ArgumentCheck{&variable.guard_message, prefix + ":guard_message"});

                if (const std::optional<Diagnostic> diagnostic =
                        make_verbatim_diagnostic(variable.value, prefix + ":value", location);
                    diagnostic.has_value())
                {
                    report.diagnostics.push_back(*diagnostic);
                    report.is_valid = false;
                }
                if (const std::optional<Diagnostic> diagnostic =
                        make_verbatim_diagnostic(variable.guard_message, prefix + ":guard_message", location);
                    diagnostic.has_value())
                {
                    report.diagnostics.push_back(*diagnostic);
                    report.is_valid = false;
                }
            }

            for (std::size_t index = 0; index < directory.global_compile_option_count(); ++index)
            {
                const std::string subject = "add_compile_options:" + std::to_string(index);
                checks.push_back(ArgumentCheck{&directory.global_compile_option_at(index), subject});
                if (const Argument* condition = directory.global_compile_option_condition_at(index); condition != nullptr)
                {
                    checks.push_back(ArgumentCheck{condition, subject + ":condition"});
                }
            }
            for (std::size_t index = 0; index < directory.language_count(); ++index)
            {
                checks.push_back(ArgumentCheck{&directory.language_at(index), "project:language:" + std::to_string(index)});
            }

            for (const ArgumentCheck& check : checks)
            {
                if (const std::optional<Diagnostic> diagnostic = make_bare_diagnostic(*check.argument, check.subject, location);
                    diagnostic.has_value())
                {
                    report.diagnostics.push_back(*diagnostic);
                    report.is_valid = false;
                }
            }
        }

        // 派生变量去重：目标派生名与同目录 Variable 名、其它目标的派生名按大写折叠比较
        void validate_derived_variables(ValidationReport& report, const Directory& directory)
        {
            const std::string location(directory.get_relative_path());

            // 按出现顺序登记的折叠名及其来源，冲突时定位到后出现者并给出先出现者
            std::vector<std::pair<std::string, std::string>> used_names;
            for (std::size_t index = 0; index < directory.variable_count(); ++index)
            {
                const Variable& variable = directory.variable_at(index);
                used_names.emplace_back(to_upper_name(variable.name.get_text()),
                                        "set 变量 " + std::string(variable.name.get_text()));
            }
            for (std::size_t index = 0; index < directory.configuration_variable_count(); ++index)
            {
                // 配置段"守卫 + 定义"同样写出一个 set，因此它也占用派生变量名
                const ConfigurationVariable& variable = directory.configuration_variable_at(index);
                used_names.emplace_back(to_upper_name(variable.name.get_text()),
                                        "配置段变量 " + std::string(variable.name.get_text()));
            }

            for (std::size_t index = 0; index < directory.target_count(); ++index)
            {
                const Target&     target = directory.target_at(index);
                const std::string upper  = to_upper_name(target.get_name());
                const std::string derived_names[2] = {upper + "_SRC_FILES", upper + "_LIBRARY_TYPE"};

                for (const std::string& derived : derived_names)
                {
                    for (const std::pair<std::string, std::string>& used : used_names)
                    {
                        if (used.first != derived)
                        {
                            continue;
                        }
                        report.diagnostics.push_back(Diagnostic{CMakeError::DuplicateDerivedVariable,
                                                               std::string(target.get_name()) + ":" + derived,
                                                               "派生变量名在同目录唯一", "与 " + used.second + " 冲突",
                                                               location,
                                                               "修改目标名或变量名，使派生变量名不重复后重跑 validate"});
                        report.is_valid = false;
                        break;
                    }
                    used_names.emplace_back(derived, "目标 " + std::string(target.get_name()) + " 的派生变量 " + derived);
                }
            }
        }

        // ---------- 工程级校验 ----------

        // 目标名唯一性按全工程判定；同目录重名是该判定的子集，不单列
        void validate_target_uniqueness(ValidationReport& report, const Project& project)
        {
            std::vector<std::pair<std::string, std::string>> declared;   // 目标名 → 首次声明位置

            for (std::size_t directory_index = 0; directory_index < project.directory_count(); ++directory_index)
            {
                const Directory& directory = project.directory_at(directory_index);
                for (std::size_t target_index = 0; target_index < directory.target_count(); ++target_index)
                {
                    const Target&     target   = directory.target_at(target_index);
                    const std::string name(target.get_name());
                    const std::string location = make_target_location(directory, target);

                    const std::string* first_location = nullptr;
                    for (const std::pair<std::string, std::string>& entry : declared)
                    {
                        if (entry.first == name)
                        {
                            first_location = &entry.second;
                            break;
                        }
                    }

                    if (first_location == nullptr)
                    {
                        declared.emplace_back(name, location);
                        continue;
                    }

                    report.diagnostics.push_back(Diagnostic{CMakeError::DuplicateTarget, name,
                                                           "目标名在全工程唯一，首次声明于 " + *first_location,
                                                           "在 " + location + " 重复声明", location,
                                                           "删除重复目标或改名后重跑 validate"});
                    report.is_valid = false;
                }
            }
        }

        // 链接库名必须能在本工程已声明的目标名或外部目标登记集合里找到
        void validate_link_targets(ValidationReport& report, const Project& project)
        {
            std::vector<std::string_view> declared_names;
            for (std::size_t directory_index = 0; directory_index < project.directory_count(); ++directory_index)
            {
                const Directory& directory = project.directory_at(directory_index);
                for (std::size_t target_index = 0; target_index < directory.target_count(); ++target_index)
                {
                    declared_names.push_back(directory.target_at(target_index).get_name());
                }
            }

            for (std::size_t directory_index = 0; directory_index < project.directory_count(); ++directory_index)
            {
                const Directory& directory = project.directory_at(directory_index);
                for (std::size_t target_index = 0; target_index < directory.target_count(); ++target_index)
                {
                    const Target&     target = directory.target_at(target_index);
                    const std::string location = make_target_location(directory, target);

                    for (std::size_t index = 0; index < target.entry_count(TargetEntryKind::LinkLibrary); ++index)
                    {
                        const ScopedEntry& entry = target.entry_at(TargetEntryKind::LinkLibrary, index);
                        const std::string_view library = entry.value.get_text();

                        bool is_known = project.has_external_target(library);
                        for (const std::string_view declared : declared_names)
                        {
                            if (declared == library)
                            {
                                is_known = true;
                                break;
                            }
                        }
                        if (is_known)
                        {
                            continue;
                        }

                        report.diagnostics.push_back(Diagnostic{CMakeError::LinkTargetNotFound,
                                                               std::string(target.get_name()) + ":" + std::string(library),
                                                               "本工程已声明的目标名或 add_external_target 登记的名字",
                                                               "未找到目标 " + std::string(library), location,
                                                               "建模该目标、用 add_external_target 登记它，或修正链接项后重跑 validate"});
                        report.is_valid = false;
                    }
                }
            }
        }
    } // namespace

    Project::Project(std::string_view name)
        : m_root(std::unique_ptr<Directory>(new Directory(name, ".")))
    {
        // 根目录的构造函数是私有的，只有 Project 与本目录的 create_subdirectory 能构造：
        // std::make_unique 的 new 表达式不在友元上下文里，因此这里直接构造后再交给 unique_ptr
    }

    std::string_view Project::get_name() const noexcept
    {
        return m_root->get_name();
    }

    Directory& Project::root() noexcept
    {
        return *m_root;
    }

    const Directory& Project::root() const noexcept
    {
        return *m_root;
    }

    std::size_t Project::directory_count() const noexcept
    {
        return count_directories(*m_root);
    }

    const Directory& Project::directory_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < directory_count());

        std::size_t      cursor = 0;
        const Directory* found  = directory_by_index(*m_root, cursor, index);
        ID_ASSERT(found != nullptr);
        return *found;
    }

    void Project::add_external_target(Argument name)
    {
        const std::string_view text = name.get_text();
        if (has_external_target(text))
        {
            return;
        }
        m_external_targets.emplace_back(text);
    }

    bool Project::has_external_target(std::string_view name) const noexcept
    {
        for (const std::string& external : m_external_targets)
        {
            if (external == name)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<OutputFile> collect_outputs(const Project& project)
    {
        std::vector<OutputFile> outputs;
        outputs.reserve(project.directory_count());

        for (std::size_t index = 0; index < project.directory_count(); ++index)
        {
            const Directory& directory = project.directory_at(index);

            // 根目录对应项目根的 CMakeLists.txt，其余目录对应 <相对路径>/CMakeLists.txt
            std::string relative_path(directory.get_relative_path());
            if (relative_path == ".")
            {
                // 根目录的相对路径是 "."，它对应项目根自己的 CMakeLists.txt
                relative_path.clear();
            }
            else
            {
                relative_path.push_back('/');
            }
            relative_path.append("CMakeLists.txt");

            outputs.push_back(OutputFile{std::move(relative_path), EmitScope::WholeFile});
        }
        return outputs;
    }

    ValidationReport validate(const Project& project)
    {
        ValidationReport report;

        // 根目录名即工程名：EmptyName 与 InvalidName 只在这里判一次，其余目录名由子目录条目判定
        const std::string root_subject = "project:" + std::string(project.root().get_name());
        const std::string root_location(project.root().get_relative_path());
        if (const std::optional<Diagnostic> diagnostic =
                make_name_diagnostic(project.root().get_name(), root_subject, root_location);
            diagnostic.has_value())
        {
            report.diagnostics.push_back(*diagnostic);
            report.is_valid = false;
        }

        for (std::size_t directory_index = 0; directory_index < project.directory_count(); ++directory_index)
        {
            const Directory& directory = project.directory_at(directory_index);

            validate_directory_content(report, directory);
            validate_derived_variables(report, directory);

            for (std::size_t target_index = 0; target_index < directory.target_count(); ++target_index)
            {
                validate_target(report, directory, directory.target_at(target_index));
            }
        }

        validate_target_uniqueness(report, project);
        validate_link_targets(report, project);

        return report;
    }
} // namespace ID::CMake
