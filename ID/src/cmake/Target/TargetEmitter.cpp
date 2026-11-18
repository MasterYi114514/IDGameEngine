#include "cmake/Target/TargetEmitter.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/Common/Assert.hpp"
#include "cmake/Common/Writer.hpp"
#include "cmake/Value/Argument.hpp"

namespace ID::CMake
{
    namespace
    {
        // 一条属性行：名字与其渲染后的取值，先收集完再统一写出，避免"先写头行再判断有没有内容"
        struct PropertyEntry
        {
            std::string name  = {};
            std::string value = {};
        };

        // 作用域关键字行：SYSTEM 是命令级选项（target_include_directories(<target> [SYSTEM] [BEFORE|AFTER]
        // <scope> ...)），只在本命令第一个作用域段之前写一次；后续作用域段再写 SYSTEM 会被 CMake 当成目录项
        std::string make_scope_line(Scope scope, bool emit_system)
        {
            std::string line;
            if (emit_system)
            {
                line.append("SYSTEM ");
            }
            line.append(scope_keyword(scope));
            return line;
        }

        // 有效的 SYSTEM 标记：SYSTEM 只对 include 目录有意义，其余三类语句（编译宏、链接目录、链接库）
        // 即使模型被误置也不会输出 SYSTEM——四条语句共用同一套分段渲染，必须在渲染处守住这条边界
        bool is_system_entry(const Target& target, TargetEntryKind kind, std::size_t index) noexcept
        {
            return kind == TargetEntryKind::IncludeDirectory && target.entry_at(kind, index).is_system;
        }

        // 条件是否可并入同一 run：空态相同且文本相同（文本比较即语义比较，不看片段类别）
        bool is_same_condition(const std::optional<Argument>& lhs, const std::optional<Argument>& rhs) noexcept
        {
            if (lhs.has_value() != rhs.has_value())
            {
                return false;
            }
            if (!lhs.has_value())
            {
                return true;
            }
            return lhs->get_text() == rhs->get_text();
        }

        // 缩进关系：命令行的缩进级数由调用方给的基线决定，作用域行 = 基线 + 1 级、取值行 = 基线 + 2 级、
        // 收尾的 ')' 回到基线。命令行与 ')' 都用 push_line，因此单独位于 if 体内时整块自动再缩进一级。
        void push_single_value_command(Writer& writer, std::string_view command, std::string_view target_name,
                                       std::string_view scope_line, std::string_view value)
        {
            writer.push_line(std::string(command) + "(" + std::string(target_name));
            writer.begin_scope();
            writer.push_line(scope_line);
            writer.begin_scope();
            writer.push_line(value);
            writer.end_scope();
            writer.end_scope();
            writer.push_line(")");
        }

        // 一种条目的全部 run：按插入顺序单遍扫描，相邻同条件条目并入同一 if 块；
        // 同一 if 块内 is_system 变化即结束当前命令、另开一条同命令（SYSTEM 是命令级选项，
        // 一条命令只能有一个 is_system 取值）；同一命令内相邻同作用域的条目并入同一作用域段
        // （重写关键字行，不重排取值）。命令之间不插空行：它们属同一项的连续语句。
        void push_scoped_entries(Writer& writer, std::string_view command, const Target& target, TargetEntryKind kind)
        {
            const std::size_t count = target.entry_count(kind);
            const std::string target_name(target.get_name());

            std::size_t index = 0;
            while (index < count)
            {
                const ScopedEntry& first   = target.entry_at(kind, index);
                std::size_t        run_end = index + 1;
                while (run_end < count && is_same_condition(first.condition, target.entry_at(kind, run_end).condition))
                {
                    ++run_end;
                }

                // 无条件 run 直接展开，不包 if；带条件 run 包成 if(<cond>) … endif()
                const bool has_condition = first.condition.has_value();
                if (has_condition)
                {
                    writer.push_unindented("if(" + render_argument(*first.condition) + ")");
                    writer.begin_scope();
                }

                // 一条命令覆盖 run 内连续同 is_system 的条目；命令内的作用域段顺序仍按插入顺序
                std::size_t command_begin = index;
                while (command_begin < run_end)
                {
                    const bool  is_system   = is_system_entry(target, kind, command_begin);
                    std::size_t command_end = command_begin + 1;
                    while (command_end < run_end && is_system_entry(target, kind, command_end) == is_system)
                    {
                        ++command_end;
                    }

                    writer.push_line(std::string(command) + "(" + target_name);

                    bool        is_command_head = true;
                    std::size_t segment_begin   = command_begin;
                    while (segment_begin < command_end)
                    {
                        const ScopedEntry& segment_first = target.entry_at(kind, segment_begin);
                        std::size_t        segment_end   = segment_begin + 1;
                        while (segment_end < command_end
                               && target.entry_at(kind, segment_end).scope == segment_first.scope)
                        {
                            ++segment_end;
                        }

                        writer.begin_scope();
                        writer.push_line(make_scope_line(segment_first.scope, is_system && is_command_head));
                        writer.begin_scope();
                        for (std::size_t value_index = segment_begin; value_index < segment_end; ++value_index)
                        {
                            writer.push_line(render_argument(target.entry_at(kind, value_index).value));
                        }
                        writer.end_scope();
                        writer.end_scope();

                        is_command_head = false;
                        segment_begin   = segment_end;
                    }

                    writer.push_line(")");
                    command_begin = command_end;
                }

                if (has_condition)
                {
                    writer.end_scope();
                    writer.push_unindented("endif()");
                }

                index = run_end;
            }
        }

        // 形态互斥宏块（第 7 项之首，P3 两段形态）：固定 if / else / endif，两分支互斥，
        // SHARED 分支 PRIVATE 定义导出宏、STATIC 分支 PUBLIC 定义静态宏；else() 只服务这里
        void push_form_option_definitions(Writer& writer, const Target& target)
        {
            const FormOption&    form        = target.get_form_option();
            const std::string    target_name(target.get_name());
            const std::string    condition   = render_argument(form.option);

            writer.push_unindented("if(" + condition + ")");
            writer.begin_scope();
            push_single_value_command(writer, "target_compile_definitions", target_name, "PRIVATE",
                                      render_argument(form.shared_define));
            writer.end_scope();

            writer.push_unindented("else()");
            writer.begin_scope();
            push_single_value_command(writer, "target_compile_definitions", target_name, "PUBLIC",
                                      render_argument(form.static_define));
            writer.end_scope();

            writer.push_unindented("endif()");
        }

        // 守卫块：目标声明前与子目录接入前共用同一形态（if(TARGET ...) message(FATAL_ERROR ...) endif()），
        // 只有这一份排版实现；DirectoryEmitter 经 make_target_guard 取同一份文本
        void push_target_guard(Writer& writer, const TargetGuard& guard)
        {
            writer.push_unindented("if(TARGET " + render_argument(guard.target) + ")");
            writer.begin_scope();
            writer.push_line("message(FATAL_ERROR " + render_argument(guard.message) + ")");
            writer.end_scope();
            writer.push_unindented("endif()");
        }

        // 分支块的一个分支：编译宏载荷复用单值命令的分段排版，变量载荷写 set(<完整参数串>)
        void push_branch_statement(Writer& writer, const Target& target, const BranchStatement& statement)
        {
            if (statement.payload == BranchPayload::CompileDefinition)
            {
                push_single_value_command(writer, "target_compile_definitions", target.get_name(),
                                          scope_keyword(statement.scope), render_argument(statement.value));
                return;
            }

            writer.push_line("set(" + render_argument(statement.value) + ")");
        }

        // 第 7 项的互斥分支块：条件由调用方给出，两分支载荷类别由 validate 保证一致，这里不做布尔求解
        void push_branch_blocks(Writer& writer, const Target& target)
        {
            for (std::size_t index = 0; index < target.branch_count(); ++index)
            {
                const BranchBlock& branch = target.branch_at(index);

                writer.push_unindented("if(" + render_argument(branch.condition) + ")");
                writer.begin_scope();
                push_branch_statement(writer, target, branch.then_statement);
                writer.end_scope();

                writer.push_unindented("else()");
                writer.begin_scope();
                push_branch_statement(writer, target, branch.else_statement);
                writer.end_scope();

                writer.push_unindented("endif()");
            }
        }

        // 第 6 项：add_library / add_executable 一行写完
        std::string make_add_line(const Target& target)
        {
            const std::string target_name(target.get_name());
            const std::string upper_name = to_upper_name(target.get_name());
            const bool        is_imported = target.has_import();
            const std::string global_part = (is_imported && target.get_import().is_global) ? " GLOBAL" : "";
            const std::string source_part = " ${" + upper_name + "_SRC_FILES}";

            if (target.get_type() == TargetType::Executable)
            {
                // imported 可执行目标没有源文件列表，GLOBAL 紧随 IMPORTED
                return is_imported ? "add_executable(" + target_name + " IMPORTED" + global_part + ")"
                                   : "add_executable(" + target_name + source_part + ")";
            }

            std::string line = "add_library(" + target_name;
            if (is_imported)
            {
                switch (target.get_type())
                {
                    case TargetType::InterfaceLibrary: line.append(" INTERFACE IMPORTED"); break;
                    case TargetType::StaticLibrary:    line.append(" STATIC IMPORTED"); break;
                    case TargetType::SharedLibrary:    line.append(" SHARED IMPORTED"); break;
                    default:                           line.append(" IMPORTED"); break;
                }
                line.append(global_part);
                line.push_back(')');
                return line;
            }

            if (target.get_type() == TargetType::InterfaceLibrary)
            {
                // INTERFACE 库没有编译单元，也没有源文件列表
                return line + " INTERFACE)";
            }
            if (target.has_form_option())
            {
                // 形态可切换目标：库类型由配置块的类型选择块决定，第 6 项只引用派生变量
                return line + " ${" + upper_name + "_LIBRARY_TYPE}" + source_part + ")";
            }

            const std::string kind = (target.get_type() == TargetType::SharedLibrary) ? " SHARED" : " STATIC";
            return line + kind + source_part + ")";
        }

        // 第 9 项：属性行。imported 目标只写产物位置；其余目标写输出目录属性与 CXX_EXTENSIONS OFF，
        // INTERFACE 库两者都不写。无属性时不写空块。
        std::vector<PropertyEntry> make_properties(const Target& target)
        {
            std::vector<PropertyEntry> properties;

            if (target.has_import())
            {
                const ImportSpec& spec = target.get_import();
                if (spec.location.has_value())
                {
                    properties.push_back(PropertyEntry{"IMPORTED_LOCATION", render_argument(*spec.location)});
                }
                if (spec.implib.has_value())
                {
                    properties.push_back(PropertyEntry{"IMPORTED_IMPLIB", render_argument(*spec.implib)});
                }
                return properties;
            }

            const TargetType type = target.get_type();
            if (type == TargetType::InterfaceLibrary)
            {
                // INTERFACE 库不产生产物：不写输出目录属性，也不写 CXX_EXTENSIONS
                return properties;
            }

            // 单目录表达与按配置表达互斥：两个都登记属调用方破坏前置条件（validate 报 InvalidConfigOutput）
            ID_ASSERT(!(target.has_output_directory() && target.has_config_output_directories()));

            // 形态可切换目标三项全写：两种形态都可能被选中，属性若按固定类型展开就与类型选择块
            // 成为两份互不一致的真源。静态分支写三项无副作用。
            const bool writes_runtime = target.has_form_option()
                                     || type == TargetType::Executable
                                     || type == TargetType::SharedLibrary;
            const bool writes_library = target.has_form_option() || type == TargetType::SharedLibrary;
            const bool writes_archive = target.has_form_option()
                                     || type == TargetType::StaticLibrary
                                     || type == TargetType::SharedLibrary;

            if (target.has_config_output_directories())
            {
                // 按配置输出属性：属性名只是在既有单目录属性名后追加配置段，取值只拼 ${ID_OUTPUT_ROOT}/debug|release；
                // 不写 $<CONFIG>、不重复展开 toolchain/arch 身份段
                const ConfigOutputDirectories& directories      = target.get_config_output_directories();
                const std::string              debug_directory   = render_argument(directories.debug);
                const std::string              release_directory = render_argument(directories.release);

                if (writes_runtime)
                {
                    properties.push_back(PropertyEntry{"RUNTIME_OUTPUT_DIRECTORY_DEBUG", debug_directory});
                    properties.push_back(PropertyEntry{"RUNTIME_OUTPUT_DIRECTORY_RELEASE", release_directory});
                }
                if (writes_library)
                {
                    properties.push_back(PropertyEntry{"LIBRARY_OUTPUT_DIRECTORY_DEBUG", debug_directory});
                    properties.push_back(PropertyEntry{"LIBRARY_OUTPUT_DIRECTORY_RELEASE", release_directory});
                }
                if (writes_archive)
                {
                    properties.push_back(PropertyEntry{"ARCHIVE_OUTPUT_DIRECTORY_DEBUG", debug_directory});
                    properties.push_back(PropertyEntry{"ARCHIVE_OUTPUT_DIRECTORY_RELEASE", release_directory});
                }
            }
            else if (target.has_output_directory())
            {
                const std::string directory = render_argument(target.get_output_directory());

                if (writes_runtime)
                {
                    properties.push_back(PropertyEntry{"RUNTIME_OUTPUT_DIRECTORY", directory});
                }
                if (writes_library)
                {
                    properties.push_back(PropertyEntry{"LIBRARY_OUTPUT_DIRECTORY", directory});
                }
                if (writes_archive)
                {
                    properties.push_back(PropertyEntry{"ARCHIVE_OUTPUT_DIRECTORY", directory});
                }
            }

            if (target.has_build_rpath())
            {
                // 多个字面目录按 CMake 列表语义用 ';' 连接成一个参数；按 QuotedExpand 渲染（转义 \ 与 "、
                // 保留 ${...} 展开），因此 $ORIGIN 写成 "$ORIGIN" 而不会被当成变量引用。
                // BUILD_RPATH 与 POSITION_INDEPENDENT_CODE 都不在 cmake-style §3、§5 列举的属性清单里，
                // 属 docs/id/07 §7.9 末段登记的"必要的补充属性"，不改契约源
                std::string joined;
                for (const std::string& directory : target.get_build_rpath())
                {
                    if (!joined.empty())
                    {
                        joined.push_back(';');
                    }
                    joined.append(directory);
                }
                properties.push_back(PropertyEntry{"BUILD_RPATH", render_argument(Argument::quoted_expand(joined))});
            }

            if (target.has_position_independent_code())
            {
                properties.push_back(PropertyEntry{"POSITION_INDEPENDENT_CODE",
                                                   target.get_position_independent_code() ? "ON" : "OFF"});
            }

            properties.push_back(PropertyEntry{"CXX_EXTENSIONS", "OFF"});
            return properties;
        }

        // 第 8 项的作用域：库目标 PUBLIC 传递编译特性，可执行目标 PRIVATE 自用
        const char* compile_features_scope(TargetType type) noexcept
        {
            return type == TargetType::Executable ? "PRIVATE" : "PUBLIC";
        }
    } // namespace

    std::string make_target_guard(const TargetGuard& guard)
    {
        Writer writer;
        push_target_guard(writer, guard);
        return std::string(writer.view());
    }

    std::string emit_target(const Target& target)
    {
        // 前置条件：生成前必须先跑 validate；哨兵类型与缺失的源通配目录都属调用方违约
        ID_ASSERT(target.get_type() != TargetType::None);

        Writer writer;

        const bool        is_imported  = target.has_import();
        const bool        is_interface = target.get_type() == TargetType::InterfaceLibrary;
        const std::string target_name(target.get_name());

        // 第 5 项：源通配。所有收源目录合成一条 file(GLOB_RECURSE)：逐目录各写一条同名变量会让后者
        // 覆盖前者。INTERFACE 库与 imported 目标没有编译单元，整块省略。
        // 收源范围按调用方登记的目录逐项展开（清单 source_dirs 不止 src 时与 cmake-style §4「只收 src/」
        // 不同），属 docs/id/07 §7.9 第 2 项登记的偏离，待契约源在 §4 补一句后失效
        bool has_item = false;
        if (!is_imported && !is_interface)
        {
            ID_ASSERT(target.has_source_glob_dir());

            const std::string upper_name = to_upper_name(target.get_name());

            writer.push_unindented("file(GLOB_RECURSE " + upper_name + "_SRC_FILES CONFIGURE_DEPENDS");
            writer.begin_scope();
            for (std::size_t index = 0; index < target.source_glob_dir_count(); ++index)
            {
                const std::string glob_dir(target.source_glob_dir_at(index));
                writer.push_line("${CMAKE_CURRENT_SOURCE_DIR}/" + glob_dir + "/*.hpp");
                writer.push_line("${CMAKE_CURRENT_SOURCE_DIR}/" + glob_dir + "/*.cpp");
            }
            writer.end_scope();
            writer.push_unindented(")");
            has_item = true;
        }

        // 守卫块：骨架第 5 与第 6 项之间独占一块。条件语句不在 cmake-style 第 2 节的项序表内，
        // 位置属 docs/id/07 §7.9 第 1 项登记的偏离，待契约源在 §2 补一句"目标声明前允许一个 TARGET 守卫块"
        if (target.has_guard())
        {
            if (has_item)
            {
                writer.push_blank_line();
            }
            push_target_guard(writer, target.get_guard());
            has_item = true;
        }

        // 第 6 项：目标声明。它恒存在，因此此后的每一项之前都写一个空行
        if (has_item)
        {
            writer.push_blank_line();
        }
        writer.push_unindented(make_add_line(target));

        // 第 7 项：形态互斥宏块在前，随后是同一条目的普通 run 块，最后是按插入顺序的互斥分支块；
        // 三段属同一项、不插空行
        if (target.has_form_option() || target.entry_count(TargetEntryKind::CompileDefinition) > 0
            || target.branch_count() > 0)
        {
            writer.push_blank_line();
            if (target.has_form_option())
            {
                push_form_option_definitions(writer, target);
            }
            push_scoped_entries(writer, "target_compile_definitions", target, TargetEntryKind::CompileDefinition);
            push_branch_blocks(writer, target);
        }

        // 第 8 项：编译特性。INTERFACE 库与 imported 目标不参与编译，整块省略
        if (!is_imported && !is_interface)
        {
            writer.push_blank_line();
            writer.push_line("target_compile_features(" + target_name);
            writer.begin_scope();
            writer.push_line(compile_features_scope(target.get_type()));
            writer.begin_scope();
            writer.push_line(render_argument(target.get_compile_standard()));
            writer.end_scope();
            writer.end_scope();
            writer.push_line(")");
        }

        // 第 9 项：set_target_properties。无属性（INTERFACE 库、无产物的 imported 目标）时整块省略
        const std::vector<PropertyEntry> properties = make_properties(target);
        if (!properties.empty())
        {
            writer.push_blank_line();
            writer.push_line("set_target_properties(" + target_name + " PROPERTIES");
            writer.begin_scope();
            for (const PropertyEntry& property : properties)
            {
                writer.push_line(property.name + " " + property.value);
            }
            writer.end_scope();
            writer.push_line(")");
        }

        // 第 10–12 项：include、link 目录、link 库。每类都是一条独立命令，按需整块省略
        struct ScopedItem
        {
            TargetEntryKind kind;
            const char*     command;
        };
        const ScopedItem scoped_items[]
        {
            ScopedItem{TargetEntryKind::IncludeDirectory, "target_include_directories"},
            ScopedItem{TargetEntryKind::LinkDirectory, "target_link_directories"},
            ScopedItem{TargetEntryKind::LinkLibrary, "target_link_libraries"},
        };

        for (const ScopedItem& item : scoped_items)
        {
            if (target.entry_count(item.kind) == 0)
            {
                continue;
            }
            writer.push_blank_line();
            push_scoped_entries(writer, item.command, target, item.kind);
        }

        return std::string(writer.view());
    }
} // namespace ID::CMake
