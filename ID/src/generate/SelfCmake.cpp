#include "generate/SelfCmake.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/Common/Assert.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Directory/DirectoryEmitter.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/Scope.hpp"
#include "cmake/Target/Target.hpp"
#include "cmake/Target/TargetType.hpp"
#include "cmake/Value/Argument.hpp"

/**
 *  仓库自描述的模型来源：一张数据表 + 一个构造函数，不做通用插件解析。
 *
 *  数据表 `kFiles` 是唯一输入：目录名、目标形态、收源目录、输出目录、宏、include、链接项、外部目标登记
 *  与两个逃生口语句都写在这里；新增插件只加一行，不新增分支。数据表里出现的每个字符串都声明了它按哪种
 *  参数片段渲染（裸值 / 加引号且保留展开 / 原始词法片段），构造函数据此调用 `Argument` 的四个工厂，
 *  因此 `Verbatim` 只出现在条件与两个逃生口，条目参数值位置不会出现它（`validate` 的 `VerbatimArgument`）。
 *
 *  与手写文件的已知语义等价差异（计划书 §7「不承诺逐字节复现」）：`${CMAKE_CURRENT_SOURCE_DIR}` 这类取值由
 *  `QuotedExpand` 承载因此带引号；不同条件各自独立 `if` 块、不合并成 `if/else`；中文注释与手工空行分组不保留；
 *  `IDWindow` 的两处配置期检查落在第 4 项末尾、vendor `add_subdirectory` 落在第 13 项。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，容器与字符串的分配成本可接受，因此不做
 *  逐点分配失败检查——这是对 `cpp-style` 第 9 节「分配失败不得忽略」的显式例外（计划书第 1.2 节第 10 项）。
 */

namespace ID::Generate
{
    namespace
    {
        // ---------- 数据表的行类型：数据载体不带逻辑（cpp-style 第 12 节） ----------

        /**
         *  `option(...)` 的一行：名字按裸值渲染，说明串按加引号渲染。
         */
        struct OptionRow
        {
            std::string_view name          = {};      // 选项名，裸值
            bool             is_default_on = false;   // 为真写 ON，否则写 OFF
            std::string_view description   = {};      // 说明串，加引号写出
        };

        /**
         *  `set(...)` 的一行。四份模型里的变量都不是 CACHE 变量，因此不写 `CACHE STRING` 段；
         *  取值按保留展开的引号渲染，`${...}` 因此仍会展开。
         */
        struct VariableRow
        {
            std::string_view name  = {};   // 变量名，裸值
            std::string_view value = {};   // 唯一取值
        };

        /**
         *  一条编译宏：作用域 + 取值 + 可选条件。条件为空串表示无条件，非空按原始词法片段承载。
         */
        struct DefineRow
        {
            CMake::Scope     scope     = CMake::Scope::Private;   // 作用域
            std::string_view value     = {};                      // 宏定义，裸值
            std::string_view condition = {};                      // 条件原文，空串表示无条件
        };

        /**
         *  一条 include 目录：`is_system` 为真时按 `SYSTEM <作用域>` 渲染（三方头的警告隔离）。
         */
        struct IncludeRow
        {
            CMake::Scope     scope     = CMake::Scope::Public;   // 作用域
            std::string_view path      = {};                     // include 路径，按保留展开的引号渲染
            bool             is_system = false;                  // 为真写 SYSTEM 关键字
        };

        /**
         *  一条链接库：目标名按裸值渲染，`validate` 按全工程已声明目标名与外部目标登记判定它是否存在。
         */
        struct LinkRow
        {
            CMake::Scope     scope     = CMake::Scope::Public;   // 作用域
            std::string_view target    = {};                     // 链接目标名，裸值
            std::string_view condition = {};                     // 条件原文，空串表示无条件
        };

        /**
         *  形态可切换目标的三项取值：`option` 必须与同文件的 `option` 行同名，否则 `validate` 报
         *  `InvalidFormOptionTarget`（`if(<未定义变量>)` 恒假，形态会静默落到静态分支）。
         */
        struct FormRow
        {
            std::string_view option        = {};   // 形态选项名
            std::string_view shared_define = {};   // SHARED 分支的导出宏（PRIVATE）
            std::string_view static_define = {};   // STATIC 分支的静态宏（PUBLIC）
        };

        /**
         *  一份 `CMakeLists.txt` 的全部内容。未填成员表示该文件没有对应的块：空 span 不写条目，
         *  空 optional 不写形态块，空串不写逃生口语句。
         */
        struct FileRow
        {
            std::string_view                  name                  = {};                          // 目录名，同时是 project() 名
            std::string_view                  directory_path        = {};                          // 相对仓库根的目录路径
            std::string_view                  target_name           = {};                          // 目标名；空串表示本文件不建模目标
            CMake::TargetType                 target_type           = CMake::TargetType::None;     // 目标类型
            std::string_view                  source_glob_dir       = {};                          // 收源目录；空串表示不设
            std::string_view                  output_directory      = {};                          // 输出目录；空串表示不设
            std::optional<FormRow>            form                  = {};                          // 形态可切换目标的三项取值
            std::span<const OptionRow>        options               = {};                          // 第 3 项
            std::span<const VariableRow>      variables             = {};                          // 第 4 项的 set 段
            std::span<const DefineRow>        defines               = {};                          // 第 7 项
            std::span<const IncludeRow>       includes              = {};                          // 第 10 项
            std::span<const LinkRow>          links                 = {};                          // 第 12 项
            std::span<const std::string_view> external_targets      = {};                          // 由别处建模、本文件只链接的目标名
            std::span<const std::string_view> subdirectories        = {};                          // 只写 add_subdirectory 的子目录
            std::string_view                  configuration_command = {};                          // 第 4 项末尾的多行原文
            std::string_view                  trailing_command      = {};                          // 第 13 项末尾的多行原文
        };

        // ---------- IDPlugin：四个文件的聚合目录，只建模 INTERFACE 目标与三条 add_subdirectory ----------

        constexpr OptionRow kIdPluginOptions[]
        {
            OptionRow{"ID_ON_DEBUG", true,
                      "Define IDWINDOW_DEBUG for IDWindow: compile IDWindow with its debug-only code path"},
        };

        constexpr VariableRow kIdPluginVariables[]
        {
            VariableRow{"IDPLUGIN_PATH", "${CMAKE_CURRENT_SOURCE_DIR}"},
            VariableRow{"IDPLUGIN_VENDOR_PATH", "${CMAKE_CURRENT_SOURCE_DIR}/Vendor"},
        };

        constexpr IncludeRow kIdPluginIncludes[]
        {
            IncludeRow{CMake::Scope::Interface, "${CMAKE_CURRENT_SOURCE_DIR}/Common", false},
        };

        // 子目录的 CMakeLists 由各自的表行建模，这里只登记第 13 项的 add_subdirectory，顺序与手写文件一致
        constexpr std::string_view kIdPluginSubdirectories[]{"IDLog", "IDJson", "IDWindow"};

        // ---------- IDJson：固定静态库 + 日志桥开关的两个条件宏与一个条件链接 ----------

        constexpr OptionRow kIdJsonOptions[]
        {
            OptionRow{"IDJSON_ENABLE_LOG", true, "Enable the IDLog error-log bridge inside IDJson"},
        };

        constexpr DefineRow kIdJsonDefines[]
        {
            // 静态库形态：公开头按「无需导入/导出声明」编译
            DefineRow{CMake::Scope::Public, "ID_JSON_STATIC", ""},
            // 手写的 if/else 两分支在模型里是两个独立条件块：run 算法只对形态选项生成 else()（§5.7），语义等价
            DefineRow{CMake::Scope::Private, "ID_JSON_ENABLE_LOG=1", "IDJSON_ENABLE_LOG"},
            DefineRow{CMake::Scope::Private, "ID_JSON_ENABLE_LOG=0", "NOT IDJSON_ENABLE_LOG"},
        };

        constexpr IncludeRow kIdJsonIncludes[]
        {
            IncludeRow{CMake::Scope::Public, "${CMAKE_CURRENT_SOURCE_DIR}/include", false},
            IncludeRow{CMake::Scope::Private, "${CMAKE_CURRENT_SOURCE_DIR}/src", false},
        };

        constexpr LinkRow kIdJsonLinks[]
        {
            // 公开头不含 IDLog 类型，因此日志桥只作用于本库自己的翻译单元
            LinkRow{CMake::Scope::Public, "IDCommonHeaders", ""},
            LinkRow{CMake::Scope::Private, "IDLog", "IDJSON_ENABLE_LOG"},
        };

        constexpr std::string_view kIdJsonExternalTargets[]{"IDCommonHeaders", "IDLog"};

        // ---------- IDLog：共享库 + 导出宏 + 三输出属性 + SYSTEM PUBLIC 三方头 ----------

        constexpr DefineRow kIdLogDefines[]
        {
            // DLL 自身必须以导出身份编译；缺少此项会让 DLL 内的符号被声明成 dllimport
            DefineRow{CMake::Scope::Private, "ID_LOG_EXPORTS", ""},
        };

        constexpr IncludeRow kIdLogIncludes[]
        {
            IncludeRow{CMake::Scope::Public, "${CMAKE_CURRENT_SOURCE_DIR}/include", false},
            IncludeRow{CMake::Scope::Private, "${CMAKE_CURRENT_SOURCE_DIR}/src", false},
            // 三方头随本目标传递；SYSTEM 让三方头的警告不进入本工程的警告策略
            IncludeRow{CMake::Scope::Public, "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include", true},
        };

        constexpr LinkRow kIdLogLinks[]
        {
            LinkRow{CMake::Scope::Public, "IDCommonHeaders", ""},
        };

        constexpr std::string_view kIdLogExternalTargets[]{"IDCommonHeaders"};

        // ---------- IDWindow：形态可切换目标 + GLFW 接入 ----------

        constexpr OptionRow kIdWindowOptions[]
        {
            OptionRow{"IDWINDOW_BUILD_SHARED", false, "Build IDWindow as a shared library"},
        };

        constexpr VariableRow kIdWindowVariables[]
        {
            VariableRow{"IDWINDOW_GLFW_DIR", "${IDPLUGIN_VENDOR_PATH}/glfw-3.5.1"},
        };

        constexpr DefineRow kIdWindowDefines[]
        {
            // 调试宏只作用于本目标，且与形态互斥宏块无先后依赖；它单独成块，不并入形态块的 else() 分支
            DefineRow{CMake::Scope::Private, "IDWINDOW_DEBUG", "ID_ON_DEBUG"},
        };

        constexpr IncludeRow kIdWindowIncludes[]
        {
            // GLFW 只出现在 src/ 里：公开头不含任何 GLFW 头，消费者不需要 GLFW 的 include 路径
            IncludeRow{CMake::Scope::Public, "${CMAKE_CURRENT_SOURCE_DIR}/include", false},
            IncludeRow{CMake::Scope::Private, "${CMAKE_CURRENT_SOURCE_DIR}/src", false},
        };

        constexpr LinkRow kIdWindowLinks[]
        {
            // GLFW 直接编入本目标：消费者不需要额外的链接项
            LinkRow{CMake::Scope::Public, "IDCommonHeaders", ""},
            LinkRow{CMake::Scope::Private, "glfw", ""},
            LinkRow{CMake::Scope::Private, "IDLog", ""},
        };

        constexpr std::string_view kIdWindowExternalTargets[]{"IDCommonHeaders", "glfw", "IDLog"};

        // 静态库是默认形态：形态选项为 OFF 时类型选择块走 STATIC 分支
        constexpr FormRow kIdWindowForm{"IDWINDOW_BUILD_SHARED", "ID_WINDOW_EXPORTS", "ID_WINDOW_STATIC"};

        // 两处配置期检查只能由第 4 项逃生口承载：模型没有 if(...)/message() 的通用表达（计划书第 1.2 节第 5 项）。
        // 内部行自带 4 空格缩进，emitter 逐行顶格写出且不剥除、不重排。
        constexpr std::string_view kIdWindowConfigurationCommand =
            "if(NOT EXISTS \"${IDWINDOW_GLFW_DIR}/CMakeLists.txt\")\n"
            "    message(FATAL_ERROR \"找不到 vendor GLFW：${IDWINDOW_GLFW_DIR}\")\n"
            "endif()\n"
            "if(TARGET glfw)\n"
            "    message(FATAL_ERROR \"glfw 目标已存在：GLFW 必须且只能由 IDWindow 接入\")\n"
            "endif()";

        // vendor 接入只能由第 13 项逃生口承载：add_subdirectory 的两个参数与 EXCLUDE_FROM_ALL 都不是目标条目
        constexpr std::string_view kIdWindowTrailingCommand =
            "add_subdirectory(\"${IDWINDOW_GLFW_DIR}\" \"${CMAKE_CURRENT_BINARY_DIR}/glfw\" EXCLUDE_FROM_ALL)";

        // ---------- 单处数据表 ----------

        constexpr std::array<FileRow, 4> kFiles
        {{
            FileRow
            {
                .name                      = "IDPlugin",
                .directory_path            = "IDPlugin",
                .target_name               = "IDCommonHeaders",
                .target_type               = CMake::TargetType::InterfaceLibrary,
                .options                   = kIdPluginOptions,
                .variables                 = kIdPluginVariables,
                .includes                  = kIdPluginIncludes,
                .subdirectories            = kIdPluginSubdirectories,
            },
            FileRow
            {
                .name                      = "IDJson",
                .directory_path            = "IDPlugin/IDJson",
                .target_name               = "IDJson",
                .target_type               = CMake::TargetType::StaticLibrary,
                .source_glob_dir           = CMake::kSourceGlobDirectory,
                .output_directory          = "${CMAKE_SOURCE_DIR}/bin",
                .options                   = kIdJsonOptions,
                .defines                   = kIdJsonDefines,
                .includes                  = kIdJsonIncludes,
                .links                     = kIdJsonLinks,
                .external_targets          = kIdJsonExternalTargets,
            },
            FileRow
            {
                .name                      = "IDLog",
                .directory_path            = "IDPlugin/IDLog",
                .target_name               = "IDLog",
                .target_type               = CMake::TargetType::SharedLibrary,
                .source_glob_dir           = CMake::kSourceGlobDirectory,
                .output_directory          = "${CMAKE_SOURCE_DIR}/bin",
                .defines                   = kIdLogDefines,
                .includes                  = kIdLogIncludes,
                .links                     = kIdLogLinks,
                .external_targets          = kIdLogExternalTargets,
            },
            FileRow
            {
                .name                      = "IDWindow",
                .directory_path            = "IDPlugin/IDWindow",
                .target_name               = "IDWindow",
                // 形态可切换目标的建模类型取默认形态（静态）；第 6 项写 ${IDWINDOW_LIBRARY_TYPE}
                .target_type               = CMake::TargetType::StaticLibrary,
                .source_glob_dir           = CMake::kSourceGlobDirectory,
                .output_directory          = "${CMAKE_SOURCE_DIR}/bin",
                .form                      = kIdWindowForm,
                .options                   = kIdWindowOptions,
                .variables                 = kIdWindowVariables,
                .defines                   = kIdWindowDefines,
                .includes                  = kIdWindowIncludes,
                .links                     = kIdWindowLinks,
                .external_targets          = kIdWindowExternalTargets,
                .configuration_command     = kIdWindowConfigurationCommand,
                .trailing_command          = kIdWindowTrailingCommand,
            },
        }};

        // ---------- 构造函数：数据表 → 模型 ----------

        // 空文本表示无条件；非空条件按原始词法片段承载，不转义、不加引号
        std::optional<CMake::Argument> make_condition(std::string_view condition)
        {
            if (condition.empty())
            {
                return std::nullopt;
            }
            return CMake::Argument::verbatim(condition);
        }

        // 目录内容：option、非 CACHE 变量、第 4 项逃生口、只写 add_subdirectory 的子目录、第 13 项逃生口
        void fill_directory(const FileRow& row, CMake::Directory& directory)
        {
            for (const OptionRow& option : row.options)
            {
                directory.add_option(CMake::Argument::bare(option.name),
                                     option.is_default_on,
                                     CMake::Argument::quoted(option.description));
            }

            for (const VariableRow& variable : row.variables)
            {
                // 非 CACHE 变量不写 CACHE STRING 段；doc_string 传空引号——bare("") 会触发 InvalidBareArgument
                directory.add_variable(CMake::Argument::bare(variable.name),
                                       std::vector<CMake::Argument>{CMake::Argument::quoted_expand(variable.value)},
                                       false,
                                       CMake::Argument::quoted(""));
            }

            if (!row.configuration_command.empty())
            {
                directory.add_configuration_command(CMake::Argument::verbatim(row.configuration_command));
            }

            for (const std::string_view name : row.subdirectories)
            {
                directory.create_referenced_subdirectory(name);
            }

            if (!row.trailing_command.empty())
            {
                directory.add_trailing_command(CMake::Argument::verbatim(row.trailing_command));
            }
        }

        // 目标内容：四项设置有值时登记，四类条目按数据表顺序追加（emitter 保序输出，不重排）
        void fill_target(const FileRow& row, CMake::Target& target)
        {
            if (!row.source_glob_dir.empty())
            {
                target.set_source_glob_dir(row.source_glob_dir);
            }
            if (!row.output_directory.empty())
            {
                // 输出目录按保留展开的引号承载，指向 ${CMAKE_SOURCE_DIR} 这类可重定位表达，不写本机路径
                target.set_output_directory(CMake::Argument::quoted_expand(row.output_directory));
            }
            if (row.form.has_value())
            {
                target.set_form_option(CMake::Argument::bare(row.form->option),
                                       CMake::Argument::bare(row.form->shared_define),
                                       CMake::Argument::bare(row.form->static_define));
            }

            for (const DefineRow& define : row.defines)
            {
                target.add_compile_definition(define.scope,
                                              CMake::Argument::bare(define.value),
                                              make_condition(define.condition));
            }

            for (const IncludeRow& include : row.includes)
            {
                target.add_include_directory(include.scope,
                                             CMake::Argument::quoted_expand(include.path),
                                             std::nullopt,
                                             include.is_system);
            }

            for (const LinkRow& link : row.links)
            {
                target.add_link_library(link.scope,
                                        CMake::Argument::bare(link.target),
                                        make_condition(link.condition));
            }
        }

        // 一份模型：根目录内容 + 一个目标 + 允许被链接的外部目标名
        std::unique_ptr<CMake::Project> make_project(const FileRow& row)
        {
            std::unique_ptr<CMake::Project> project = std::make_unique<CMake::Project>(row.name);

            fill_directory(row, project->root());

            if (!row.target_name.empty())
            {
                CMake::Target& target = project->root().create_target(row.target_name, row.target_type);
                fill_target(row, target);
            }

            for (const std::string_view external : row.external_targets)
            {
                project->add_external_target(CMake::Argument::bare(external));
            }

            return project;
        }

        // 落盘路径：数据表目录（相对仓库根）+ `collect_outputs` 给出的相对路径。
        // 规则：`OutputFile::relative_path` 以「本行建模的那个目录」为根——根目录对应 "CMakeLists.txt"，
        // 其子目录对应 "<子目录>/CMakeLists.txt"，即已经包含建模目录之下的全部目录段和文件名（计划书 §5.6）。
        // 因此 `directory_path` 只作前缀拼一次，既不重复拼目录名，也不自行追加文件名或子目录段。
        std::string make_output_path(const FileRow& row, std::string_view relative_path)
        {
            // 调用方违约（空路径、绝对路径）用断言表达：它不是可诊断的输入错误，而是清单与数据表不一致
            ID_ASSERT(!relative_path.empty() && relative_path.front() != '/');

            std::string path(row.directory_path);
            path.push_back('/');
            path.append(relative_path);
            return path;
        }
    } // namespace

    SelfBundle make_self_bundle()
    {
        SelfBundle bundle;

        // 先构造全部模型并逐个校验：validate 失败时不产出文本，emit_* 的前置断言因此不会被触发
        std::vector<std::unique_ptr<CMake::Project>> projects;
        projects.reserve(kFiles.size());

        for (const FileRow& row : kFiles)
        {
            std::unique_ptr<CMake::Project> project = make_project(row);

            const CMake::ValidationReport report = CMake::validate(*project);
            if (!report.is_valid)
            {
                bundle.report.is_valid = false;
                for (const CMake::Diagnostic& diagnostic : report.diagnostics)
                {
                    bundle.report.diagnostics.push_back(diagnostic);
                }
            }

            projects.push_back(std::move(project));
        }

        if (!bundle.report.is_valid)
        {
            return bundle;
        }

        // 校验通过后逐目录产出整文件文本；清单顺序与 emit 顺序都取自 collect_outputs 的深度优先序列
        for (std::size_t row_index = 0; row_index < kFiles.size(); ++row_index)
        {
            const CMake::Project&   project = *projects[row_index];
            const std::vector<CMake::OutputFile> outputs = CMake::collect_outputs(project);

            for (std::size_t index = 0; index < outputs.size(); ++index)
            {
                const CMake::Directory& directory = project.directory_at(index);

                // 仓库内建模文件：不传 GeneratedHeader，首行即 cmake_minimum_required
                std::string content = CMake::emit_directory(directory, outputs[index].scope, nullptr);

                // 落盘路径只看 collect_outputs 的 relative_path，不再从目录对象另拼一次目录段
                bundle.files.push_back(
                    SelfFile{make_output_path(kFiles[row_index], outputs[index].relative_path), std::move(content)});
            }
        }

        return bundle;
    }
} // namespace ID::Generate
