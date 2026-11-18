#include "cmake/Target/Target.hpp"

#include <utility>

#include "cmake/Common/Assert.hpp"

namespace ID::CMake
{
    namespace
    {
        // 空源通配目录的返回目标：静态存储、进程生命周期内有效，data() 不为空指针
        constexpr char kEmptyGlobDir[] = "";

        // 派生变量名折叠：只处理 ASCII 小写字母，不调用 locale 相关接口（行为与平台区域设置无关）
        std::string fold_ascii_upper(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (const char character : text)
            {
                const bool is_lower = character >= 'a' && character <= 'z';
                result.push_back(is_lower ? static_cast<char>(character - 'a' + 'A') : character);
            }
            return result;
        }
    } // namespace

    std::string to_upper_name(std::string_view name)
    {
        return fold_ascii_upper(name);
    }

    Target::Target(std::string_view name, TargetType type)
        : m_name(name)
        , m_type(type)
    {
    }

    std::string_view Target::get_name() const noexcept
    {
        return m_name;
    }

    TargetType Target::get_type() const noexcept
    {
        return m_type;
    }

    std::string_view Target::get_source_glob_dir() const noexcept
    {
        if (m_source_glob_dirs.empty())
        {
            // 空目标返回空串：指向静态空字面量而不是默认构造的空视图，调用方转 std::string 时不经过空指针
            return std::string_view(kEmptyGlobDir);
        }
        return m_source_glob_dirs.front();
    }

    bool Target::has_source_glob_dir() const noexcept
    {
        return !m_source_glob_dirs.empty();
    }

    std::size_t Target::source_glob_dir_count() const noexcept
    {
        return m_source_glob_dirs.size();
    }

    std::string_view Target::source_glob_dir_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_source_glob_dirs.size());
        return m_source_glob_dirs[index];
    }

    bool Target::has_config_output_directories() const noexcept
    {
        return m_has_config_output_directories;
    }

    const ConfigOutputDirectories& Target::get_config_output_directories() const noexcept
    {
        return m_config_output_directories;
    }

    bool Target::has_guard() const noexcept
    {
        return m_has_guard;
    }

    const TargetGuard& Target::get_guard() const noexcept
    {
        return m_guard;
    }

    std::size_t Target::branch_count() const noexcept
    {
        return m_branches.size();
    }

    const BranchBlock& Target::branch_at(std::size_t index) const noexcept
    {
        ID_ASSERT(index < m_branches.size());
        return m_branches[index];
    }

    bool Target::has_build_rpath() const noexcept
    {
        return m_has_build_rpath;
    }

    const std::vector<std::string>& Target::get_build_rpath() const noexcept
    {
        return m_build_rpath;
    }

    bool Target::has_position_independent_code() const noexcept
    {
        return m_has_position_independent_code;
    }

    bool Target::get_position_independent_code() const noexcept
    {
        return m_position_independent_code;
    }

    bool Target::has_compile_standard() const noexcept
    {
        return m_has_compile_standard;
    }

    const Argument& Target::get_compile_standard() const noexcept
    {
        return m_compile_standard;
    }

    bool Target::has_output_directory() const noexcept
    {
        return m_has_output_directory;
    }

    const Argument& Target::get_output_directory() const noexcept
    {
        return m_output_directory;
    }

    bool Target::has_form_option() const noexcept
    {
        return m_has_form_option;
    }

    const FormOption& Target::get_form_option() const noexcept
    {
        return m_form_option;
    }

    bool Target::has_import() const noexcept
    {
        return m_has_import;
    }

    const ImportSpec& Target::get_import() const noexcept
    {
        return m_import;
    }

    std::size_t Target::entry_count(TargetEntryKind kind) const noexcept
    {
        ID_ASSERT(kind != TargetEntryKind::Count);
        return m_entries[to_target_entry_kind_uint(kind)].size();
    }

    const ScopedEntry& Target::entry_at(TargetEntryKind kind, std::size_t index) const noexcept
    {
        ID_ASSERT(kind != TargetEntryKind::Count);

        const std::vector<ScopedEntry>& entries = m_entries[to_target_entry_kind_uint(kind)];
        ID_ASSERT(index < entries.size());
        return entries[index];
    }

    void Target::add_source_glob_dir(std::string_view relative_dir)
    {
        m_source_glob_dirs.emplace_back(relative_dir);
    }

    void Target::set_source_glob_dir(std::string_view relative_dir)
    {
        // 兼容包装：清空后只加一条，既有单目录调用点因此不需要改，多目录调用走 add_source_glob_dir
        m_source_glob_dirs.clear();
        m_source_glob_dirs.emplace_back(relative_dir);
    }

    void Target::set_config_output_directories(Argument debug_directory, Argument release_directory)
    {
        m_config_output_directories.debug   = std::move(debug_directory);
        m_config_output_directories.release = std::move(release_directory);
        m_has_config_output_directories     = true;
    }

    void Target::set_guard(Argument target, Argument message)
    {
        m_guard.target = std::move(target);
        m_guard.message = std::move(message);
        m_has_guard     = true;
    }

    void Target::add_branch(Argument condition, BranchStatement then_statement, BranchStatement else_statement)
    {
        m_branches.push_back(BranchBlock{std::move(condition), std::move(then_statement), std::move(else_statement)});
    }

    void Target::set_build_rpath(std::vector<std::string> directories)
    {
        m_build_rpath     = std::move(directories);
        m_has_build_rpath = true;
    }

    void Target::set_position_independent_code(bool enabled)
    {
        m_position_independent_code     = enabled;
        m_has_position_independent_code = true;
    }

    void Target::set_compile_standard(Argument feature)
    {
        m_compile_standard     = std::move(feature);
        m_has_compile_standard = true;
    }

    void Target::set_output_directory(Argument directory)
    {
        m_output_directory     = std::move(directory);
        m_has_output_directory = true;
    }

    void Target::add_compile_definition(Scope scope, Argument definition, std::optional<Argument> condition)
    {
        append_entry(TargetEntryKind::CompileDefinition, ScopedEntry{scope, std::move(definition), std::move(condition), false});
    }

    void Target::add_include_directory(Scope scope, Argument path, std::optional<Argument> condition, bool is_system)
    {
        append_entry(TargetEntryKind::IncludeDirectory, ScopedEntry{scope, std::move(path), std::move(condition), is_system});
    }

    void Target::add_link_directory(Scope scope, Argument path, std::optional<Argument> condition)
    {
        append_entry(TargetEntryKind::LinkDirectory, ScopedEntry{scope, std::move(path), std::move(condition), false});
    }

    void Target::add_link_library(Scope scope, Argument library, std::optional<Argument> condition)
    {
        append_entry(TargetEntryKind::LinkLibrary, ScopedEntry{scope, std::move(library), std::move(condition), false});
    }

    void Target::set_form_option(Argument option, Argument shared_define, Argument static_define)
    {
        m_form_option.option        = std::move(option);
        m_form_option.shared_define = std::move(shared_define);
        m_form_option.static_define = std::move(static_define);
        m_has_form_option           = true;
    }

    void Target::set_import(std::optional<Argument> location, std::optional<Argument> implib, bool is_global)
    {
        m_import.location  = std::move(location);
        m_import.implib    = std::move(implib);
        m_import.is_global = is_global;
        m_has_import       = true;
    }

    void Target::append_entry(TargetEntryKind kind, ScopedEntry entry)
    {
        // Count 不是合法类别；is_system 只对 include 目录有意义，其它类别上置真属调用方违约
        ID_ASSERT(kind != TargetEntryKind::Count);
        ID_ASSERT(!entry.is_system || kind == TargetEntryKind::IncludeDirectory);

        m_entries[to_target_entry_kind_uint(kind)].push_back(std::move(entry));
    }
} // namespace ID::CMake
