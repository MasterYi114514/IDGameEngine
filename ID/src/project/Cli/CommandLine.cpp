#include "project/Cli/CommandLine.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Text.hpp"

/**
 *  `ID project` 的参数词法实现（docs/id/10 §10.3）。
 *
 *  表驱动的参数集合：每个子命令允许哪些参数、哪些必须给，都在下面的判定函数里一次写清；解析循环只负责
 *  "取到值、查重、记原始文本"。互斥与必填在循环结束后统一判定，因此诊断顺序不依赖参数书写顺序。
 */

namespace ID::Project
{
    namespace
    {
        // 可复制的调用形式：诊断的下一步必须能照着跑
        constexpr std::string_view kNewPluginForm =
            "ID project new --name <项目名> --path <绝对路径> --plugin IDJson";
        constexpr std::string_view kNewEmptyForm =
            "ID project new --name <项目名> --path <绝对路径> --empty";
        constexpr std::string_view kBuildForm =
            "ID project build <项目名> --generator \"MinGW Makefiles\" --toolchain mingw --arch x64 --config debug";

        ProjectDiagnostic make_usage(std::string subject, std::string actual, std::string next_action)
        {
            return ProjectDiagnostic{ProjectCategory::CliUsage, std::move(subject),
                                     "符合 ID project 的用法文本", std::move(actual), {},
                                     std::move(next_action)};
        }

        CommandParse fail(CommandParse result, std::string subject, std::string actual, std::string next_action)
        {
            result.is_ok = false;
            result.diagnostics.add(make_usage(std::move(subject), std::move(actual), std::move(next_action)));
            return result;
        }

        // 子命令文本 → 取值；未知返回空 optional
        std::optional<Subcommand> parse_subcommand(std::string_view text)
        {
            if (text == "new")
            {
                return Subcommand::New;
            }
            if (text == "update")
            {
                return Subcommand::Update;
            }
            if (text == "build")
            {
                return Subcommand::Build;
            }
            if (text == "clean")
            {
                return Subcommand::Clean;
            }
            if (text == "recover")
            {
                return Subcommand::Recover;
            }
            if (text == "list")
            {
                return Subcommand::List;
            }
            return std::nullopt;
        }

        // 参数是否被该子命令接受：不在表内的参数一律按未知参数拒绝，不做静默忽略
        bool is_option_allowed(Subcommand command, std::string_view option) noexcept
        {
            if (option == "--check" || option == "--dry-run")
            {
                return command != Subcommand::List;
            }
            if (option == "--name" || option == "--path" || option == "--plugin" || option == "--empty")
            {
                return command == Subcommand::New;
            }
            if (option == "--build")
            {
                return command == Subcommand::New || command == Subcommand::Update;
            }
            if (option == "--force")
            {
                // new 只为不在用法层拒绝而接受该参数：非空未登记目录一律 project_exists，--force 不放行
                // （spec §2.2、§9.2）；它也不改变 new 的冲突判定。
                return command == Subcommand::New || command == Subcommand::Update || command == Subcommand::Clean;
            }
            if (option == "--config" || option == "--generator" || option == "--toolchain" || option == "--arch"
                || option == "--cmake")
            {
                return command == Subcommand::New || command == Subcommand::Update || command == Subcommand::Build;
            }
            if (option == "--action")
            {
                return command == Subcommand::Recover;
            }
            return false;
        }

        // 取同名参数的已记取值：用于"重复且取值不同"的判定
        struct AssignedValue
        {
            std::string_view name  = {};
            std::string      value = {};
        };

        bool assign_once(std::vector<AssignedValue>& assigned, std::string_view name, const std::string& value,
                         std::string& conflict_actual)
        {
            for (const AssignedValue& item : assigned)
            {
                if (item.name != name)
                {
                    continue;
                }
                if (item.value == value)
                {
                    return true;   // 取值相同视为同一请求
                }
                conflict_actual = std::string(name) + " 重复给出且取值不同：" + item.value + " / " + value;
                return false;
            }

            AssignedValue entry;
            entry.name  = name;
            entry.value = value;
            assigned.push_back(std::move(entry));
            return true;
        }

        std::string_view usage_text() noexcept
        {
            return "用法: ID project <子命令> [参数]\n"
                   "子命令:\n"
                   "  new --name <项目名> --path <绝对路径> (--plugin <spec>... | --empty) [--check] [--build]\n"
                   "      [--force] [--config debug|release] [--generator <name>] [--toolchain <tag>] "
                   "[--arch <tag>] [--cmake <path>]\n"
                   "  update <项目名> [--check] [--build] [--config ...] [--generator ...] [--toolchain ...] "
                   "[--arch ...] [--cmake ...] [--force]\n"
                   "  build <项目名> --config debug|release [--generator ...] [--toolchain ...] [--arch ...] "
                   "[--cmake ...] [--check]\n"
                   "  clean <项目名> [--check] [--force]\n"
                   "  recover <项目名> --action continue|restore [--check]\n"
                   "  list\n"
                   "参数:\n"
                   "  --plugin <Name>[:static|dynamic][:OPTION=ON,OPTION2=OFF]   可重复，一个参数只描述一个插件\n"
                   "  --empty                                                   显式声明不选任何插件\n"
                   "  --check / --dry-run                                       只读：输出计划差异，不写任何文件\n"
                   "  --build                                                   生成后继续构建（new / update）\n"
                   "  --force                                                   仅 update / clean 生效；new 接受该参数但不放行非空目录\n"
                   "退出码: 0 成功 / 1 输入或一致性校验失败 / 2 写入、记录、恢复或并发失败 / 3 用法错误 /\n"
                   "        4 构建环境或构建失败 / 5 清理被阻塞 / 6 --check 检出计划差异\n";
        }
    } // namespace

    std::string_view project_usage() noexcept
    {
        return usage_text();
    }

    CommandParse parse_command_line(std::span<const std::string> arguments)
    {
        CommandParse result;

        if (arguments.empty())
        {
            return fail(std::move(result), "project", "缺少子命令",
                        "改用 " + std::string(kNewPluginForm) + "、ID project list 或 ID project update <项目名>");
        }

        const std::optional<Subcommand> subcommand = parse_subcommand(arguments[0]);
        if (!subcommand.has_value())
        {
            return fail(std::move(result), arguments[0], "未知子命令",
                        "子命令只能是 new / update / build / clean / recover / list");
        }

        CommandRequest&    request = result.request;
        request.command            = *subcommand;

        std::vector<AssignedValue> assigned;
        std::size_t                index = 1;

        while (index < arguments.size())
        {
            const std::string_view argument(arguments[index]);

            if (!starts_with(argument, "--"))
            {
                // 位置参数：只有非 new/list 的命令接受一个项目名
                if (request.command == Subcommand::New)
                {
                    return fail(std::move(result), std::string(argument),
                                "new 不收位置参数（项目名由 --name 给出）",
                                "改用 " + std::string(kNewPluginForm));
                }
                if (request.command == Subcommand::List)
                {
                    return fail(std::move(result), std::string(argument), "list 不收任何参数",
                                "直接运行 ID project list");
                }
                if (!request.name.empty())
                {
                    return fail(std::move(result), std::string(argument),
                                "只允许一个项目名位置参数", "删除多余的位置参数后重跑");
                }
                request.name = std::string(argument);
                ++index;
                continue;
            }

            if (!is_option_allowed(request.command, argument))
            {
                return fail(std::move(result), std::string(argument),
                            "该参数不被当前子命令接受",
                            "去掉该参数或改用接受它的子命令；" + std::string(kBuildForm));
            }

            // 开关类参数
            if (argument == "--check" || argument == "--dry-run")
            {
                request.is_check = true;
                ++index;
                continue;
            }
            if (argument == "--build")
            {
                request.is_build = true;
                ++index;
                continue;
            }
            if (argument == "--force")
            {
                request.is_force = true;
                ++index;
                continue;
            }
            if (argument == "--empty")
            {
                request.is_empty = true;
                ++index;
                continue;
            }

            // 取值类参数
            if (index + 1 >= arguments.size())
            {
                return fail(std::move(result), std::string(argument), "缺少取值",
                            "在 " + std::string(argument) + " 之后给出取值；" + std::string(kBuildForm));
            }

            const std::string value = arguments[index + 1];
            index += 2;

            if (argument == "--plugin")
            {
                PluginSpecParse parsed = parse_plugin_spec(value);
                if (!parsed.is_ok)
                {
                    return fail(std::move(result), value, parsed.error,
                                "按 <Name>[:static|dynamic][:OPTION=ON,...] 给出，例如 "
                                    + std::string(kNewPluginForm));
                }
                request.plugins.push_back(std::move(parsed.spec));
                continue;
            }

            std::string conflict;
            if (!assign_once(assigned, argument, value, conflict))
            {
                return fail(std::move(result), std::string(argument), conflict, "只给一个取值后重跑");
            }

            if (argument == "--name")
            {
                request.name = value;
            }
            else if (argument == "--path")
            {
                request.path = value;
            }
            else if (argument == "--config")
            {
                request.config = value;
            }
            else if (argument == "--generator")
            {
                request.generator = value;
            }
            else if (argument == "--toolchain")
            {
                request.toolchain = value;
            }
            else if (argument == "--arch")
            {
                request.arch = value;
            }
            else if (argument == "--cmake")
            {
                request.cmake = value;
            }
            else if (argument == "--action")
            {
                request.action = value;
            }
        }

        // 互斥组合：与书写顺序无关
        if (request.is_check && request.is_build)
        {
            return fail(std::move(result), "--check --build",
                        "--check 与 --build 不能同时给出（只读与执行互斥）",
                        "只要检查结果就去掉 --build，要真正构建就去掉 --check");
        }
        if (!request.plugins.empty() && request.is_empty)
        {
            return fail(std::move(result), "--plugin --empty",
                        "--plugin 与 --empty 不能同时给出",
                        "二选一：给 --plugin <spec>（可重复）或 --empty");
        }

        // 同一插件名重复：不合并、不去重、不后者覆盖前者
        for (std::size_t left = 0; left < request.plugins.size(); ++left)
        {
            for (std::size_t right = left + 1; right < request.plugins.size(); ++right)
            {
                if (request.plugins[left].name != request.plugins[right].name)
                {
                    continue;
                }
                return fail(std::move(result), request.plugins[right].name,
                            "同一插件名在一次命令里出现两次（不合并、不去重）",
                            "只写一次该插件，并在同一参数里给出它的 linkage 与选项");
            }
        }

        // 必填项
        if (request.command == Subcommand::New)
        {
            if (request.name.empty())
            {
                return fail(std::move(result), "--name", "new 缺少 --name",
                            "补齐项目名后重跑：" + std::string(kNewPluginForm));
            }
            if (request.path.empty())
            {
                return fail(std::move(result), "--path", "new 缺少 --path（项目根的绝对路径）",
                            "补齐项目根后重跑：" + std::string(kNewPluginForm));
            }
        }
        else if (request.command != Subcommand::List && request.name.empty())
        {
            return fail(std::move(result), "项目名", "缺少项目名位置参数",
                        "按 ID project <子命令> <项目名> 重跑；先用 ID project list 查看已登记项目");
        }

        if (request.command == Subcommand::Build && request.config.empty())
        {
            return fail(std::move(result), "--config", "build 必须显式给出 debug 或 release",
                        "重跑并显式给出 --config：" + std::string(kBuildForm));
        }

        if (request.command == Subcommand::Recover)
        {
            if (request.action.empty())
            {
                return fail(std::move(result), "--action", "recover 必须显式给出 continue 或 restore",
                            "按 ID project recover <项目名> --action continue 重跑（或 --action restore）");
            }
            if (request.action != "continue" && request.action != "restore")
            {
                return fail(std::move(result), request.action, "--action 只能是 continue 或 restore",
                            "按 ID project recover <项目名> --action continue 重跑（或 --action restore）");
            }
        }

        result.is_ok = true;
        return result;
    }
} // namespace ID::Project
