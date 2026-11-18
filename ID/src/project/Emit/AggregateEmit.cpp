#include "project/Emit/AggregateEmit.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/Directory/Directory.hpp"
#include "cmake/Directory/DirectoryEmitter.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/Scope.hpp"
#include "cmake/Target/Target.hpp"
#include "cmake/Target/TargetType.hpp"
#include "cmake/Value/Argument.hpp"
#include "project/Log/Log.hpp"

/**
 *  聚合构建描述的模型装配：一个 `INTERFACE` 汇总目标 + 集中 vendor 接入 + 拓扑序 `add_subdirectory`
 *  + 尾部 vendor 运行期输出属性适配块。
 *
 *  两条"只做一次"：vendor 目录按 `dir` 去重、只接入一次（重复 `add_subdirectory` 会让三方目标重定义）；
 *  vendor 目标按 `target` 去重、只写一段适配块。插件子目录的顺序直接取 `Resolution.init_order`，
 *  不排序、不按目录扫描顺序，两者不一致属实现缺陷。
 *
 *  vendor 适配块是骨架第 13 项之后的受控逃生口（`docs/id/07` §7.9 第 4 项登记的偏离）：生成期不知道
 *  vendor 目标的类型，因此按"可能产生运行期文件的类型"写 `RUNTIME_` / `LIBRARY_` 的四个属性，
 *  不适用于该类型的属性由 CMake 自身忽略；`Vendor/**` 本身不被改写。
 */

namespace ID::Project
{
    namespace
    {
        // 聚合目录名与它自己的构建描述文件名
        constexpr std::string_view kAggregateDirectory = "IDPlugin";
        constexpr std::string_view kAggregateFilePath  = "IDPlugin/CMakeLists.txt";

        // 汇总目标暴露的 Common 头 include 根（相对 IDPlugin/）
        constexpr std::string_view kCommonIncludePath = "${CMAKE_CURRENT_SOURCE_DIR}/Common";

        // vendor 源目录与二进制目录表达式：源在 IDPlugin/Vendor/<dir>，构建树不混进工程根构建目录
        constexpr std::string_view kVendorSourcePrefix = "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/";
        constexpr std::string_view kVendorBinaryPrefix = "${CMAKE_CURRENT_BINARY_DIR}/";

        // 适配块的属性取值：只拼配置段，身份段由根片段的 ID_OUTPUT_ROOT 承担
        constexpr std::string_view kDebugOutputRoot   = "${ID_OUTPUT_ROOT}/debug";
        constexpr std::string_view kReleaseOutputRoot = "${ID_OUTPUT_ROOT}/release";

        // 拒绝守卫与降级限制的固定文本
        constexpr std::string_view kVendorGuardTail =
            " 已存在：共享 vendor 目录只允许在本文件集中接入一次";
        constexpr std::string_view kVendorMissingTarget = "<未记录>";
        constexpr std::string_view kNotAdaptedCode      = "vendor_runtime_output_not_adapted";

        // 两种降级原因：条目自检不通过，与"目录在 vendor_dirs 里却没有任何可接入目标"
        constexpr std::string_view kVendorReasonOutOfScope =
            "该条目不在 Resolution.vendor_dirs 与 NamingPlan.vendor_targets 的交叉范围内";
        constexpr std::string_view kVendorReasonNoTarget =
            "没有任何编译面事实给出该目录的可接入目标";

        /**
         *  一条共享 vendor 接入：目录名与该目录提供的主要目标名。
         */
        struct VendorEntry
        {
            std::string dir    = { };
            std::string target = { };
        };

        bool contains_name(const std::vector<std::string>& names, std::string_view name) noexcept
        {
            for (const std::string& entry : names)
            {
                if (std::string_view(entry) == name)
                {
                    return true;
                }
            }
            return false;
        }

        // 共享 vendor 条目：来自各处 `compile_facts` 的 `vendor_links`，按 (dir, target) 去重后排序
        std::vector<VendorEntry> collect_vendor_entries(const Resolution& resolution)
        {
            std::vector<VendorEntry> entries;
            for (const PluginCompileFacts& facts : resolution.compile_facts)
            {
                for (const VendorLink& link : facts.vendor_links)
                {
                    bool is_known = false;
                    for (const VendorEntry& entry : entries)
                    {
                        if (entry.dir == link.dir && entry.target == link.target)
                        {
                            is_known = true;
                            break;
                        }
                    }
                    if (is_known)
                    {
                        continue;
                    }
                    entries.push_back(VendorEntry{link.dir, link.target});
                }
            }

            std::sort(entries.begin(), entries.end(),
                      [](const VendorEntry& left, const VendorEntry& right) noexcept
                      {
                          if (left.dir != right.dir)
                          {
                              return std::string_view(left.dir) < std::string_view(right.dir);
                          }
                          return std::string_view(left.target) < std::string_view(right.target);
                      });
            return entries;
        }

        // 自检：dir 与 target 非空、dir 在 vendor_dirs 中、target 在 vendor_targets 中
        bool is_adaptable(const ResolvedAssembly& assembly, const VendorEntry& entry) noexcept
        {
            if (entry.dir.empty() || entry.target.empty())
            {
                return false;
            }
            if (!contains_name(assembly.resolution.vendor_dirs, entry.dir))
            {
                return false;
            }
            return contains_name(assembly.naming.vendor_targets, entry.target);
        }

        // 降级是"状态不符合预期但可继续"：记一条 warn，并登记 limitations（不进入 DiagnosticSet）。
        // `reason` 写明本次为什么不能自证：说明串必须同时给出 dir 与 target，不许只写"未适配"
        void report_not_adaptable(std::vector<EmitLimitation>& limitations, std::string_view dir, std::string_view target,
                                  std::string_view reason)
        {
            EmitLimitation limitation;
            limitation.code = std::string(kNotAdaptedCode);
            limitation.message = "vendor 目录 ";
            limitation.message.append(dir.empty() ? kVendorMissingTarget : dir);
            limitation.message.append(" 的目标 ");
            limitation.message.append(target.empty() ? kVendorMissingTarget : target);
            limitation.message.append(" 无法自证运行期产物适配（");
            limitation.message.append(reason);
            limitation.message.append(
                "）：本次不写它的接入与运行期输出属性适配块，其产物不保证与可执行文件同目录"
                "（spec §11.6 末条）");
            limitations.push_back(std::move(limitation));

            // 日志器可能尚未初始化（例如直接链接 IDCore 的消费者），此处显式判空后再用宏
            if (IDProject_logger == nullptr)
            {
                return;
            }
            const std::string dir_text(dir.empty() ? kVendorMissingTarget : dir);
            const std::string target_text(target.empty() ? kVendorMissingTarget : target);
            IDPROJECT_LOG_WARN("emit_aggregate: vendor 目标无法自证运行期产物适配: dir={}, target={}", dir_text,
                               target_text);
        }

        // 接入前的拒绝守卫说明串：单行、含目标名
        std::string make_vendor_guard_message(std::string_view target)
        {
            std::string message("目标 ");
            message.append(target);
            message.append(kVendorGuardTail);
            return message;
        }

        std::string make_vendor_source(std::string_view dir)
        {
            std::string expression(kVendorSourcePrefix);
            expression.append(dir);
            return expression;
        }

        std::string make_vendor_binary(std::string_view dir)
        {
            std::string expression(kVendorBinaryPrefix);
            expression.append(dir);
            return expression;
        }

        // 尾部适配块：四项属性指向与可执行文件相同的配置目录；不写 $<CONFIG>、不写全局输出变量
        std::string make_adaptation_command(std::string_view target)
        {
            std::string command("set_target_properties(");
            command.append(target);
            command.append(" PROPERTIES\n");
            command.append("    RUNTIME_OUTPUT_DIRECTORY_DEBUG \"");
            command.append(kDebugOutputRoot);
            command.append("\"\n");
            command.append("    RUNTIME_OUTPUT_DIRECTORY_RELEASE \"");
            command.append(kReleaseOutputRoot);
            command.append("\"\n");
            command.append("    LIBRARY_OUTPUT_DIRECTORY_DEBUG \"");
            command.append(kDebugOutputRoot);
            command.append("\"\n");
            command.append("    LIBRARY_OUTPUT_DIRECTORY_RELEASE \"");
            command.append(kReleaseOutputRoot);
            command.append("\"\n");
            command.push_back(')');
            return command;
        }
    } // namespace

    AggregateEmitResult emit_aggregate(const ResolvedAssembly& assembly)
    {
        AggregateEmitResult result;

        // 用花括号避免 most vexing parse：`Project(std::string(...))` 会被读成函数声明
        CMake::Project    project{std::string(kAggregateDirectory)};
        CMake::Directory& root = project.root();

        // 第 6 项：汇总目标取命名计划的名字；INTERFACE 库没有编译单元，不设输出属性与编译特性
        CMake::Target& common = root.create_target(assembly.naming.common_target,
                                                   CMake::TargetType::InterfaceLibrary);

        // 第 10 项：Common 头的 include 根随目标传递给所有消费者
        common.add_include_directory(CMake::Scope::Interface,
                                     CMake::Argument::quoted_expand(std::string(kCommonIncludePath)));

        // 第 13 项之前：共享 vendor 的集中接入（按 dir 去重，只接入一次）。
        // 同一个 dir 只登记一条降级：条目自检失败与"目录没有可接入目标"不重复报两遍
        const std::vector<VendorEntry> vendors = collect_vendor_entries(assembly.resolution);
        std::vector<std::string>       connected_dirs;
        std::vector<std::string>       reported_dirs;

        for (const VendorEntry& vendor : vendors)
        {
            if (!is_adaptable(assembly, vendor))
            {
                if (!contains_name(reported_dirs, vendor.dir))
                {
                    report_not_adaptable(result.limitations, vendor.dir, vendor.target, kVendorReasonOutOfScope);
                    reported_dirs.push_back(vendor.dir);
                }
                continue;
            }
            if (contains_name(connected_dirs, vendor.dir))
            {
                continue;
            }

            root.create_referenced_subdirectory(
                CMake::Argument::quoted_expand(make_vendor_source(vendor.dir)),
                CMake::Argument::quoted_expand(make_vendor_binary(vendor.dir)),
                true,
                CMake::TargetGuard{CMake::Argument::bare(vendor.target),
                                   CMake::Argument::quoted(make_vendor_guard_message(vendor.target))});
            connected_dirs.push_back(vendor.dir);
        }

        // vendor_dirs 里有、却没有任何编译面事实给出目标的目录：既不能接入也不能写守卫，如实登记降级
        for (const std::string& dir : assembly.resolution.vendor_dirs)
        {
            if (contains_name(connected_dirs, dir) || contains_name(reported_dirs, dir))
            {
                continue;
            }
            report_not_adaptable(result.limitations, dir, std::string_view{}, kVendorReasonNoTarget);
            reported_dirs.push_back(dir);
        }

        // 第 13 项：插件子目录，顺序等于拓扑序（被依赖者在前）
        for (const std::string& plugin : assembly.resolution.init_order)
        {
            root.create_referenced_subdirectory(plugin);
        }

        // 第 13 项尾部：全部 add_subdirectory 之后写 vendor 运行期输出属性适配块
        for (const VendorEntry& vendor : vendors)
        {
            if (!is_adaptable(assembly, vendor))
            {
                continue;
            }
            root.add_trailing_command(CMake::Argument::verbatim(make_adaptation_command(vendor.target)));
        }

        result.report = CMake::validate(project);
        if (!result.report.is_valid)
        {
            return result;
        }

        result.aggregate.target_relative = std::string(kAggregateFilePath);
        result.aggregate.text            = CMake::emit_directory(root, CMake::EmitScope::WholeFile);
        return result;
    }
} // namespace ID::Project
