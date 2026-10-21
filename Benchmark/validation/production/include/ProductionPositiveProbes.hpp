#pragma once

#include <cstdint>

/**
 *  正向对照 TU 的对外探针（Benchmark/validation/production，不是生产代码）
 *  1. 每个正向 TU 导出一个具名 noexcept 函数，返回该 TU 内 constexpr 计划的关键字段：
 *     ProductionCaseControls 逐个调用并核对期望值，因此"6 个正向 TU 真的进入链接行"
 *     有真实符号引用可查，而不是只看目标构建成功；
 *  2. 生产 API 的三个操作族都是 consteval（register_plugin / declare_dependencies /
 *     sort_and_validate），运行期无法重新构造计划；探针只读取编译期已算好的结果，
 *     不复制断言、不绕过 API、不引入 P3 动态装配；
 *  3. 编译期 static_assert 仍由编译器在编译这些 TU 时校验；探针是运行期第二道读数校验。
 */
namespace ID::Bench::Production::Positive
{
    struct ProbeResult
    {
        uint16_t active_count    = 0;
        uint16_t edge_count      = 0;
        uint16_t provision_count = 0;
        uint8_t  error_code      = 0;        // PluginError 的底层值，0 表示 None
    };

    ProbeResult basics() noexcept;
    ProbeResult graph() noexcept;
    ProbeResult ownership() noexcept;
    ProbeResult permutation() noexcept;
    ProbeResult storage() noexcept;
    ProbeResult uneven() noexcept;
} // namespace ID::Bench::Production::Positive
