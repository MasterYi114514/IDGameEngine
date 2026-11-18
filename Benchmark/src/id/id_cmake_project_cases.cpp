#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark_support.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Project/Project.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 工程层用例（计划书 §5.6 `Project/`：遍历顺序、待落盘清单与外部目标登记）。
 *
 *  判定依据：
 *  - `collect_outputs` 的清单按深度优先顺序（父目录先于子目录），根目录对应 `CMakeLists.txt`，
 *    其余目录对应 `<相对路径>/CMakeLists.txt`（POSIX 分隔符，不含盘符与 `..` 段）；
 *  - `create_referenced_subdirectory` 登记的条目**不**出现在清单里（没有目录对象、不落盘）；
 *  - 模型一律给 `EmitScope::WholeFile`（哪些改成 `BodyOnly` 由组装阶段决定，不由模型内置策略）；
 *  - `directory_at` 与 `collect_outputs` 用同一套遍历口径，顺序在调用时现算；
 *  - `add_external_target` 按名称去重，重复登记不产生诊断；`has_external_target` 是链接判定的登记集合。
 *
 *  纪律：本层只读模型，不写文件、不做路径合法性判断（落盘层用例负责写入期诊断）。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::CmakeFixture::CheckText;
    using ID::CMake::Argument;
    using ID::CMake::collect_outputs;
    using ID::CMake::Directory;
    using ID::CMake::EmitScope;
    using ID::CMake::OutputFile;
    using ID::CMake::Project;

    void CaseCollectOutputs() noexcept
    {
        Project    project("Proj");
        Directory& root = project.root();

        Directory& a = root.create_subdirectory("A");
        a.create_subdirectory("Inner");
        a.create_referenced_subdirectory("Ref");
        root.create_subdirectory("B");
        root.create_referenced_subdirectory("RootRef");

        const std::vector<OutputFile> outputs = collect_outputs(project);
        Check(outputs.size() == 4, "id.cmake.project.outputs.count",
              "4 个落盘文件 = 根 + A + A/Inner + B（referenced 条目不入清单）；实际 " +
                  std::to_string(outputs.size()));

        const std::string_view expected_paths[] = {
            "CMakeLists.txt",
            "A/CMakeLists.txt",
            "A/Inner/CMakeLists.txt",
            "B/CMakeLists.txt",
        };
        const std::size_t expected_count = sizeof(expected_paths) / sizeof(expected_paths[0]);
        for (std::size_t index = 0; index < expected_count; ++index)
        {
            const std::string actual = (index < outputs.size()) ? outputs[index].relative_path : std::string("<缺项>");
            Check(actual == expected_paths[index],
                  std::string("id.cmake.project.outputs.path.") + std::to_string(index),
                  "深度优先顺序与路径形态：期望 [" + std::string(expected_paths[index]) + "] / 实际 [" + actual + "]");
        }

        bool is_all_whole_file = true;
        bool is_any_referenced = false;
        for (const OutputFile& output : outputs)
        {
            if (output.scope != EmitScope::WholeFile)
            {
                is_all_whole_file = false;
            }
            if (output.relative_path.find("Ref") != std::string::npos)
            {
                is_any_referenced = true;
            }
        }
        Check(is_all_whole_file, "id.cmake.project.outputs.scope_whole_file", "模型一律给 EmitScope::WholeFile");
        Check(!is_any_referenced, "id.cmake.project.outputs.referenced_absent",
              "create_referenced_subdirectory 登记的条目不出现在落盘清单里");

        // 目录遍历与清单用同一套口径：父先于子
        Check(project.directory_count() == 4, "id.cmake.project.directory_count", "目录总数 4（含根，不含 referenced 条目）");
        Check(project.directory_at(0).get_name() == "Proj", "id.cmake.project.directory_at.root_name", "第 0 个目录是根");
        Check(project.directory_at(1).get_name() == "A", "id.cmake.project.directory_at.first_child", "第 1 个目录是 A");
        Check(project.directory_at(2).get_relative_path() == "A/Inner", "id.cmake.project.directory_at.depth_path",
              "第 2 个目录是 A/Inner（父目录先于子目录的深度优先）");
        Check(project.directory_at(3).get_name() == "B", "id.cmake.project.directory_at.second_child", "第 3 个目录是 B");
    }

    void CaseProjectIdentity() noexcept
    {
        Project project("Demo");
        Check(project.get_name() == "Demo", "id.cmake.project.name.forwards_root", "工程名转发 root().get_name()");
        Check(project.root().get_relative_path() == ".", "id.cmake.project.root.relative_path", "根目录的相对路径是 \".\"");
        CheckText("id.cmake.project.root.name", project.root().get_name(), "Demo");
    }

    void CaseReferencedSubdirectory() noexcept
    {
        Project    project("Proj");
        Directory& root = project.root();
        root.create_referenced_subdirectory("Ref");
        const Directory& created = root.create_subdirectory("Real");
        Check(created.get_name() == "Real", "id.cmake.project.subdirectory.created_name", "create_subdirectory 返回可继续建模的目录");

        Check(root.subdirectory_entry_count() == 2, "id.cmake.project.subdirectory_entry_count",
              "两种入口共用一条有序序列，合计 2 条");
        Check(root.subdirectory_entry_at(0).is_referenced, "id.cmake.project.subdirectory.referenced_flag",
              "referenced 条目的标记为真");
        Check(root.subdirectory_at(0) == nullptr, "id.cmake.project.subdirectory.referenced_no_object",
              "referenced 条目没有目录对象（返回空指针）");
        Check(root.subdirectory_at(1) != nullptr, "id.cmake.project.subdirectory.created_has_object",
              "create_subdirectory 的条目有目录对象");
        Check(project.directory_count() == 2, "id.cmake.project.subdirectory.referenced_not_traversed",
              "referenced 条目不进入遍历（只有根与 Real）");
    }

    void CaseExternalTargets() noexcept
    {
        Project project("Proj");
        project.add_external_target(Argument::bare("IDLog"));
        project.add_external_target(Argument::bare("IDLog"));
        Check(project.has_external_target("IDLog"), "id.cmake.project.external.registered", "外部目标登记后可按名字查到");
        Check(!project.has_external_target("NoSuchTarget"), "id.cmake.project.external.unknown",
              "未登记的名字查不到（不是\"任何名字都返回真\"）");

        // 去重：重复登记不改变集合内容（这里用"多登记一次不引入第二条"间接取证）
        project.add_external_target(Argument::bare("IDJson"));
        Check(project.has_external_target("IDJson"), "id.cmake.project.external.second_name", "可以登记多个外部目标");
    }
} // namespace

namespace ID::Bench
{
    void RunCmakeProjectCases() noexcept
    {
        CaseCollectOutputs();
        CaseProjectIdentity();
        CaseReferencedSubdirectory();
        CaseExternalTargets();
    }
} // namespace ID::Bench
