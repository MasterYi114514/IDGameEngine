#include <cstdio>

#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  合法对照可执行文件（Benchmark/validation/production，不是生产代码）
 *  1. 全部反向用例 TU（70 条）的 PRODUCTION_CASE_LEGAL 分支与 6 个正向 TU 一起编成
 *     OBJECT 库并真正进入本可执行文件的链接行（驱动器读取链接响应文件核对对象数）；
 *  2. 合法分支的校验全部是编译期 static_assert，没有可调用符号，因此本程序
 *     不声称"运行期调用过这些合法分支"；6 个正向 TU 显式导出探针函数，本程序逐个
 *     调用并核对 active/edge/provision/error 四个读数，链接与运行期引用都有真实证据；
 *  3. 生产 API 的三个操作族都是 consteval，运行期无法重新构造计划；这里校验的是
 *     编译期结果的运行期读数，不绕过 API、不做 P3 动态装配、不分配堆内存。
 */

namespace
{
    using ID::Bench::Production::Positive::ProbeResult;

    struct ProbeExpectation
    {
        const char*          name     = nullptr;
        ProbeResult (*probe)() noexcept = nullptr;
        ProbeResult          expected = {};
    };

    // 期望值来自各正向 TU 的编译期 static_assert 口径（同一份计划，运行期再核一次）
    constexpr ProbeExpectation kExpectations[] = {
        { "positive_basics", &ID::Bench::Production::Positive::basics,
          ProbeResult{ 3, 0, 3, 0 } },
        { "positive_graph", &ID::Bench::Production::Positive::graph,
          ProbeResult{ 2, 1, 2, 0 } },
        { "positive_ownership", &ID::Bench::Production::Positive::ownership,
          ProbeResult{ 1, 0, 5, 0 } },
        { "positive_permutation", &ID::Bench::Production::Positive::permutation,
          ProbeResult{ 2, 1, 3, 0 } },
        { "positive_storage", &ID::Bench::Production::Positive::storage,
          ProbeResult{ 2, 1, 2, 0 } },
        { "positive_uneven", &ID::Bench::Production::Positive::uneven,
          ProbeResult{ 4, 2, 4, 0 } },
    };

    bool same_result(const ProbeResult& actual, const ProbeResult& expected) noexcept
    {
        return actual.active_count == expected.active_count && actual.edge_count == expected.edge_count &&
               actual.provision_count == expected.provision_count && actual.error_code == expected.error_code;
    }
} // namespace

int main()
{
    int checks   = 0;
    int failures = 0;

    for (const ProbeExpectation& expectation : kExpectations)
    {
        const ProbeResult actual = expectation.probe();
        ++checks;
        const bool ok = same_result(actual, expectation.expected);
        if (!ok)
        {
            ++failures;
        }
        std::printf("  [%s] %-24s active=%u edge=%u provision=%u error=%u\n", ok ? "PASS" : "FAIL", expectation.name,
                    static_cast<unsigned>(actual.active_count), static_cast<unsigned>(actual.edge_count),
                    static_cast<unsigned>(actual.provision_count), static_cast<unsigned>(actual.error_code));
        if (!ok)
        {
            std::printf("      expected active=%u edge=%u provision=%u error=%u\n",
                        static_cast<unsigned>(expectation.expected.active_count),
                        static_cast<unsigned>(expectation.expected.edge_count),
                        static_cast<unsigned>(expectation.expected.provision_count),
                        static_cast<unsigned>(expectation.expected.error_code));
        }
    }

    // 结构修订是公共契约的一部分；运行期确认控制程序与生产头链接一致
    const bool schema_ok = ID::PluginCompileTime::kSchemaRevision == 1;
    ++checks;
    if (!schema_ok)
    {
        ++failures;
    }
    std::printf("  [%s] %-24s schema_revision=%u\n", schema_ok ? "PASS" : "FAIL", "production_schema",
                static_cast<unsigned>(ID::PluginCompileTime::kSchemaRevision));

    std::printf("production-case-controls: %d passed / %d failed\n", checks - failures, failures);
    return failures == 0 ? 0 : 1;
}
