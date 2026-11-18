#include "cmake/Directory/DirectoryEmitter.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include "cmake/Common/Assert.hpp"
#include "cmake/Common/Writer.hpp"
#include "cmake/Target/TargetEmitter.hpp"

namespace ID::CMake
{
    namespace
    {
        // 逐行顶格写出：文本自身的缩进原样保留，emitter 不合并、不重排、不剥除前导空格。
        // 末尾换行不产生额外的空行（"a\n" 只有一行内容）。
        void push_multiline(Writer& writer, std::string_view text)
        {
            std::size_t begin = 0;
            while (begin < text.size())
            {
                const std::size_t end = text.find('\n', begin);
                if (end == std::string_view::npos)
                {
                    writer.push_unindented(text.substr(begin));
                    return;
                }
                writer.push_unindented(text.substr(begin, end - begin));
                begin = end + 1;
            }
        }

        // project(<名>[ LANGUAGES <lang>...])：语言参数为空时省略 LANGUAGES 段
        std::string make_project_line(const Directory& directory)
        {
            std::string line = "project(" + std::string(directory.get_name());
            if (directory.language_count() > 0)
            {
                line.append(" LANGUAGES");
                for (std::size_t index = 0; index < directory.language_count(); ++index)
                {
                    line.push_back(' ');
                    line.append(render_argument(directory.language_at(index)));
                }
            }
            line.push_back(')');
            return line;
        }

        // option(<名> <说明> ON|OFF)：一条一行，块内不插空行
        void push_options(Writer& writer, const Directory& directory)
        {
            for (std::size_t index = 0; index < directory.option_count(); ++index)
            {
                const Option& option = directory.option_at(index);

                std::string line = "option(";
                line.append(render_argument(option.name));
                line.push_back(' ');
                line.append(render_argument(option.description));
                line.append(option.is_default_on ? " ON)" : " OFF)");
                writer.push_unindented(line);
            }
        }

        // 目录级全局编译选项：与目标条目同一条 run 算法（相邻同条件并入同一块），但没有作用域段
        void push_global_compile_options(Writer& writer, const Directory& directory)
        {
            const std::size_t count = directory.global_compile_option_count();

            std::size_t index = 0;
            while (index < count)
            {
                const Argument* condition = directory.global_compile_option_condition_at(index);
                std::size_t     run_end   = index + 1;
                while (run_end < count)
                {
                    const Argument* candidate = directory.global_compile_option_condition_at(run_end);
                    const bool      is_lhs_empty = condition == nullptr;
                    const bool      is_rhs_empty = candidate == nullptr;
                    if (is_lhs_empty != is_rhs_empty)
                    {
                        break;
                    }
                    if (!is_lhs_empty && condition->get_text() != candidate->get_text())
                    {
                        break;
                    }
                    ++run_end;
                }

                const bool has_condition = condition != nullptr;
                if (has_condition)
                {
                    writer.push_unindented("if(" + render_argument(*condition) + ")");
                    writer.begin_scope();
                }

                std::string line = "add_compile_options(";
                for (std::size_t value_index = index; value_index < run_end; ++value_index)
                {
                    if (value_index != index)
                    {
                        line.push_back(' ');
                    }
                    line.append(render_argument(directory.global_compile_option_at(value_index)));
                }
                line.push_back(')');
                writer.push_line(line);

                if (has_condition)
                {
                    writer.end_scope();
                    writer.push_unindented("endif()");
                }

                index = run_end;
            }
        }

        // 库类型选择块：属第 4 项，写在源通配与 add_library 之前。位置固定，按目标创建顺序排列
        void push_library_type_selection(Writer& writer, const Target& target)
        {
            const std::string type_variable = to_upper_name(target.get_name()) + "_LIBRARY_TYPE";

            writer.push_unindented("if(" + render_argument(target.get_form_option().option) + ")");
            writer.begin_scope();
            writer.push_line("set(" + type_variable + " SHARED)");
            writer.end_scope();
            writer.push_unindented("else()");
            writer.begin_scope();
            writer.push_line("set(" + type_variable + " STATIC)");
            writer.end_scope();
            writer.push_unindented("endif()");
        }

        // 配置块是否有内容：除变量、全局编译选项与配置块命令外，形态可切换目标各自的类型选择块也算
        bool has_form_option_target(const Directory& directory) noexcept
        {
            for (std::size_t index = 0; index < directory.target_count(); ++index)
            {
                if (directory.target_at(index).has_form_option())
                {
                    return true;
                }
            }
            return false;
        }

        // 第 4 项配置块：set 变量 → 配置段"守卫 + 定义" → add_compile_options → 各目标的类型选择块 →
        // 配置块命令，块内不插空行。唯一例外是配置段"守卫 + 定义"：守卫块独占一块，与它那条 set 之间空一行
        void push_configuration_block(Writer& writer, const Directory& directory)
        {
            bool has_written = false;

            for (std::size_t index = 0; index < directory.variable_count(); ++index)
            {
                const Variable& variable = directory.variable_at(index);

                std::string line = "set(";
                line.append(render_argument(variable.name));
                for (const Argument& value : variable.values)
                {
                    line.push_back(' ');
                    line.append(render_argument(value));
                }
                if (variable.is_cache)
                {
                    line.append(" CACHE STRING ");
                    line.append(render_argument(variable.doc_string));
                }
                line.push_back(')');
                writer.push_unindented(line);
                has_written = true;
            }

            for (std::size_t index = 0; index < directory.configuration_variable_count(); ++index)
            {
                const ConfigurationVariable& variable = directory.configuration_variable_at(index);

                if (has_written)
                {
                    writer.push_blank_line();
                }

                // 守卫固定为拒绝：变量为空时直接 message(FATAL_ERROR)，不给默认值、不提供 fallback
                writer.push_unindented("if(" + render_argument(variable.guard_condition) + ")");
                writer.begin_scope();
                writer.push_line("message(FATAL_ERROR " + render_argument(variable.guard_message) + ")");
                writer.end_scope();
                writer.push_unindented("endif()");
                writer.push_blank_line();

                writer.push_unindented("set(" + render_argument(variable.name) + " " + render_argument(variable.value) + ")");
                has_written = true;
            }

            push_global_compile_options(writer, directory);

            for (std::size_t index = 0; index < directory.target_count(); ++index)
            {
                const Target& target = directory.target_at(index);
                if (!target.has_form_option())
                {
                    continue;
                }
                push_library_type_selection(writer, target);
            }

            for (std::size_t index = 0; index < directory.configuration_command_count(); ++index)
            {
                push_multiline(writer, directory.configuration_command_at(index).get_text());
            }
        }

        // 第 13 项：add_subdirectory 按条目顺序在前，尾部命令在后；跨类不做排序
        void push_trailing_block(Writer& writer, const Directory& directory)
        {
            for (std::size_t index = 0; index < directory.subdirectory_entry_count(); ++index)
            {
                const SubdirectoryEntry& entry = directory.subdirectory_entry_at(index);

                if (entry.guard.has_value())
                {
                    // 守卫紧贴在自己那条 add_subdirectory 之前，与它之间一个空行；排版与目标守卫同一份实现。
                    // cmake-style §2 第 13 项只列命令种类，没有登记守卫与接入命令的相对位置：
                    // 属 docs/id/07 §7.9 第 3 项登记的偏离，待契约源在 §2 第 13 项补一句
                    push_multiline(writer, make_target_guard(*entry.guard));
                    writer.push_blank_line();
                }

                std::string line = "add_subdirectory(";
                if (entry.source.has_value())
                {
                    // 带源目录表达式的条目：名称由表达式承载，name 只作条目身份
                    line.append(render_argument(*entry.source));
                }
                else
                {
                    line.append(entry.name);
                }
                if (entry.binary.has_value())
                {
                    line.push_back(' ');
                    line.append(render_argument(*entry.binary));
                }
                if (entry.is_excluded_from_all)
                {
                    line.append(" EXCLUDE_FROM_ALL");
                }
                line.push_back(')');
                writer.push_unindented(line);
            }

            for (std::size_t index = 0; index < directory.trailing_command_count(); ++index)
            {
                push_multiline(writer, directory.trailing_command_at(index).get_text());
            }
        }
    } // namespace

    std::string emit_directory(const Directory& directory, EmitScope scope, const GeneratedHeader* header)
    {
        // 调用方违约：只有整文件形态带注释头；受控片段（BodyOnly 与三个分段 scope）都只能配空 header
        ID_ASSERT(!(scope != EmitScope::WholeFile && header != nullptr));
        if (header != nullptr)
        {
            // 注释头取值按 get_text() 原样写出，因此 Verbatim 不适用（它不是原始词法片段的位置）
            ID_ASSERT(header->tool_name.get_kind() != ArgumentKind::Verbatim);
            ID_ASSERT(header->source_record.get_kind() != ArgumentKind::Verbatim);
        }

        // 范围开关：五个取值共用同一套骨架顺序实现，只按项分段输出。HeaderOnly / ConfigurationOnly /
        // TargetsOnly 依次拼接（段与段之间一个空行）就是 WholeFile 的正文
        const bool writes_file_head     = scope == EmitScope::WholeFile || scope == EmitScope::HeaderOnly;
        const bool writes_configuration = scope != EmitScope::HeaderOnly && scope != EmitScope::TargetsOnly;
        const bool writes_targets       = scope != EmitScope::HeaderOnly && scope != EmitScope::ConfigurationOnly;

        Writer writer;
        bool   has_block = false;

        // 注释头：两行各占一行，其后空一行再写第 1 项（两个不同的块之间空一行）
        if (header != nullptr)
        {
            writer.push_unindented("# 由 " + std::string(header->tool_name.get_text()) + " 生成，请勿手工修改");
            writer.push_unindented("# 来源：" + std::string(header->source_record.get_text()));
            has_block = true;
        }

        // 第 1、2 项：整文件与 HeaderOnly 形态才有；两者之间空一行，取值由 cmake-style 第 2 节固定
        if (writes_file_head)
        {
            if (has_block)
            {
                writer.push_blank_line();
            }
            writer.push_unindented("cmake_minimum_required(VERSION 4.1.0)");
            writer.push_blank_line();
            writer.push_unindented(make_project_line(directory));
            has_block = true;
        }

        // 第 3 项：option 块
        if (writes_configuration && directory.option_count() > 0)
        {
            if (has_block)
            {
                writer.push_blank_line();
            }
            push_options(writer, directory);
            has_block = true;
        }

        // 第 4 项：配置块
        const bool has_configuration = directory.variable_count() > 0
                                    || directory.configuration_variable_count() > 0
                                    || directory.global_compile_option_count() > 0
                                    || directory.configuration_command_count() > 0
                                    || has_form_option_target(directory);
        if (writes_configuration && has_configuration)
        {
            if (has_block)
            {
                writer.push_blank_line();
            }
            push_configuration_block(writer, directory);
            has_block = true;
        }

        // 第 5–12 项：每个目标一整块，块与块之间空一行
        if (writes_targets)
        {
            for (std::size_t index = 0; index < directory.target_count(); ++index)
            {
                if (has_block)
                {
                    writer.push_blank_line();
                }
                push_multiline(writer, emit_target(directory.target_at(index)));
                has_block = true;
            }
        }

        // 第 13 项：add_subdirectory 与尾部命令
        if (writes_targets
            && (directory.subdirectory_entry_count() > 0 || directory.trailing_command_count() > 0))
        {
            if (has_block)
            {
                writer.push_blank_line();
            }
            push_trailing_block(writer, directory);
        }

        return std::string(writer.view());
    }
} // namespace ID::CMake
