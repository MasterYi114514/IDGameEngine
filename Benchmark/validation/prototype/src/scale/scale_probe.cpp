#include "FixtureStressSupport.hpp"

/**
 *  容量边界探针：同一个 TU，用 -D 覆盖四个容量参数，逐点测量"默认常量求值限制"下的合法上界
 *  1. 默认档故意设为已验证通过的 64/128/32/4，保证本 TU 在普通构建里也成立；
 *  2. 边界测量由直接调用编译器完成，完整命令、退出码与编译输出保存在
 *     Benchmark/validation/baseline/raw/scale-probe-*.log，复现索引见
 *     Benchmark/validation/prototype-p0-evidence.txt 第 3 节与第 6 节；
 *     不使用 -fconstexpr-ops-limit 之类的放宽参数通过任何用例，避免用提高上限掩盖算法成本。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

#if !defined(IDF_PROBE_ACTIVE)
    #define IDF_PROBE_ACTIVE       64
    #define IDF_PROBE_CATALOG      128
    #define IDF_PROBE_REQUIREMENTS 32
    #define IDF_PROBE_CANDIDATES   4
#endif

    inline constexpr StressProfile kProfile{ IDF_PROBE_ACTIVE, IDF_PROBE_CATALOG,
                                             IDF_PROBE_REQUIREMENTS, IDF_PROBE_CANDIDATES };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    static_assert(kPlan.error == ErrorCode::None, "scale_probe: 该容量组合必须合法");
    static_assert(kPlan.active_count == IDF_PROBE_ACTIVE, "scale_probe: active 数必须与配置一致");
} // namespace
