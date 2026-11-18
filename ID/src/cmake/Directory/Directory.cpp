#include "cmake/Directory/Directory.hpp"

#include <utility>

#include "cmake/Common/Assert.hpp"

namespace ID::CMake
{
    namespace
    {
        // 子目录的相对路径：根目录的相对路径是 "."，它的子目录直接以名字作为相对路径
        std::string make_child_relative_path(std::string_view parent, std::string_view name)
        {
            if (parent == ".")
            {
                return std::string(name);
            }

            std::string result(parent);
            result.push_back('/');
            result.append(name);
            return result;
        }
    } // namespace

    Directory::Directory(std::string_view name, std::string_view relative_path)
        : m_name(name)
        , m_relative_path(relative_path)
    {
    }

    std::string_view Directory::get_name() const noexcept
    {
        return m_name;
    }

    std::string_view Directory::get_relative_path() const noexcept
    {
        return m_relative_path;
    }

    std::size_t Directory::target_count() const noexcept
    {
        return m_targets.size();
    }

    const Target& Directory::target_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_targets.size());
        return *m_targets[index];
    }

    std::size_t Directory::subdirectory_entry_count() const noexcept
    {
        return m_subdirectory_entries.size();
    }

    const SubdirectoryEntry& Directory::subdirectory_entry_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_subdirectory_entries.size());
        return m_subdirectory_entries[index];
    }

    const Directory* Directory::subdirectory_at(std::size_t entry_index) const noexcept
    {
        ID_ASSERT(entry_index < m_subdirectory_entries.size());
        if (m_subdirectory_entries[entry_index].is_referenced)
        {
            // 只登记名字的条目没有目录对象：不进入落盘遍历，也不能在其下继续建模
            return nullptr;
        }

        // 目录对象按 is_referenced 为假的条目顺序入容器，因此下标由前序非 referenced 条目数给出
        std::size_t object_index = 0;
        for (std::size_t index = 0; index < entry_index; ++index)
        {
            if (!m_subdirectory_entries[index].is_referenced)
            {
                ++object_index;
            }
        }

        ID_ASSERT(object_index < m_subdirectories.size());
        return m_subdirectories[object_index].get();
    }

    std::size_t Directory::option_count() const noexcept
    {
        return m_options.size();
    }

    const Option& Directory::option_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_options.size());
        return m_options[index];
    }

    std::size_t Directory::variable_count() const noexcept
    {
        return m_variables.size();
    }

    const Variable& Directory::variable_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_variables.size());
        return m_variables[index];
    }

    std::size_t Directory::configuration_variable_count() const noexcept
    {
        return m_configuration_variables.size();
    }

    const ConfigurationVariable& Directory::configuration_variable_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_configuration_variables.size());
        return m_configuration_variables[index];
    }

    std::size_t Directory::global_compile_option_count() const noexcept
    {
        return m_global_compile_options.size();
    }

    const Argument& Directory::global_compile_option_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_global_compile_options.size());
        return m_global_compile_options[index];
    }

    const Argument* Directory::global_compile_option_condition_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_global_compile_option_conditions.size());
        if (!m_global_compile_option_conditions[index].has_value())
        {
            // 空指针表示无条件，调用方不得解引用
            return nullptr;
        }
        return &*m_global_compile_option_conditions[index];
    }

    std::size_t Directory::configuration_command_count() const noexcept
    {
        return m_configuration_commands.size();
    }

    const Argument& Directory::configuration_command_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_configuration_commands.size());
        return m_configuration_commands[index];
    }

    std::size_t Directory::trailing_command_count() const noexcept
    {
        return m_trailing_commands.size();
    }

    const Argument& Directory::trailing_command_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_trailing_commands.size());
        return m_trailing_commands[index];
    }

    std::size_t Directory::language_count() const noexcept
    {
        return m_languages.size();
    }

    const Argument& Directory::language_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_languages.size());
        return m_languages[index];
    }

    void Directory::add_language(Argument language)
    {
        m_languages.push_back(std::move(language));
    }

    void Directory::add_option(Argument name, bool is_default_on, Argument description)
    {
        m_options.push_back(Option{std::move(name), is_default_on, std::move(description)});
    }

    void Directory::add_variable(Argument name, std::vector<Argument> values, bool is_cache, Argument doc_string)
    {
        m_variables.push_back(Variable{std::move(name), std::move(values), is_cache, std::move(doc_string)});
    }

    void Directory::add_configuration_variable(Argument name, Argument value, Argument guard_condition, Argument guard_message)
    {
        // 每个目录至多一条：重复调用属调用方违约（守卫块与 set 的配对关系只按单条定义），不进退诊断通道
        ID_ASSERT(m_configuration_variables.empty());

        m_configuration_variables.push_back(
            ConfigurationVariable{std::move(name), std::move(value), std::move(guard_condition), std::move(guard_message)});
    }

    void Directory::add_global_compile_option(Argument option, std::optional<Argument> condition)
    {
        m_global_compile_options.push_back(std::move(option));
        m_global_compile_option_conditions.push_back(std::move(condition));
    }

    void Directory::add_configuration_command(Argument command_line)
    {
        m_configuration_commands.push_back(std::move(command_line));
    }

    Target& Directory::create_target(std::string_view name, TargetType type)
    {
        // 唯一的构造入口：对象在堆上独立存在，容器扩容不移动它，返回的引用因此长期有效。
        // Target 的构造函数是私有的（Directory 是友元），而 std::make_unique 的 new 表达式不在友元
        // 上下文里，因此在这里直接构造后再交给 unique_ptr。
        m_targets.push_back(std::unique_ptr<Target>(new Target(name, type)));
        return *m_targets.back();
    }

    Directory& Directory::create_subdirectory(std::string_view name)
    {
        const std::string name_text(name);

        // 两种入口共用同一条有序序列：条目顺序就是第 13 项 add_subdirectory 的输出顺序
        m_subdirectory_entries.push_back(SubdirectoryEntry{name_text, false});
        m_subdirectories.push_back(std::unique_ptr<Directory>(
            new Directory(name_text, make_child_relative_path(m_relative_path, name_text))));
        return *m_subdirectories.back();
    }

    void Directory::create_referenced_subdirectory(std::string_view name)
    {
        // 只写 add_subdirectory：不建目录对象、不进入落盘遍历、不能在其下继续建模
        m_subdirectory_entries.push_back(SubdirectoryEntry{std::string(name), true});
    }

    void Directory::create_referenced_subdirectory(Argument source,
                                                   Argument binary,
                                                   bool is_excluded_from_all,
                                                   std::optional<TargetGuard> guard)
    {
        // 只写 add_subdirectory：不建目录对象、不进入落盘遍历、不能在其下继续建模。
        // 条目身份取源目录表达式的文本（去重与诊断定位用它），写出时按表达式渲染
        SubdirectoryEntry entry;
        entry.name                 = std::string(source.get_text());
        entry.is_referenced        = true;
        entry.source               = std::move(source);
        entry.is_excluded_from_all = is_excluded_from_all;
        entry.guard                = std::move(guard);
        if (!binary.get_text().empty())
        {
            // 空片段表示"不写该参数"：写出来会变成 add_subdirectory("" ...) 那样的空参数
            entry.binary = std::move(binary);
        }

        m_subdirectory_entries.push_back(std::move(entry));
    }

    void Directory::add_trailing_command(Argument command_line)
    {
        m_trailing_commands.push_back(std::move(command_line));
    }
} // namespace ID::CMake
