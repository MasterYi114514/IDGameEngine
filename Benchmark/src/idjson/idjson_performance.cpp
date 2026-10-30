#include "IDJson.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark_support.hpp"
#include "idjson_testdata.hpp"

/**
 *  IDJson 性能用例。
 *
 *  纪律（与 methodology / grading 一致）：
 *  - 每项指标先预热（不计时，至少 30 ms），再固定 5 轮计时；单轮目标时长 120 ms（methodology §2 要求 ≥ 100 ms），
 *    迭代数由一次"标定 pass"按实测单次耗时反算，因此 Debug/Release 两种变体都能落在同一轮时长窗口。
 *    报告**中位数**；最小/最大/极差与迭代数写进每项 note。进程内极差 > 20% 标 unstable、basis 留空 →
 *    ungraded（grading.md §5/§6：不把 unstable 当有效评级）。
 *  - 固定输入：三份合成文档（配置类 / 大对象数组 / 深嵌套+长字符串）由 `idjson_testdata.hpp` 按固定参数拼装，
 *    没有随机数与时间依赖；迭代数只由标定决定，不参与"挑数字"。
 *  - 防优化：每轮把结果的真实内容累加进 `checksum`，循环外 `KeepAlive` 写入跨 TU sink；计时区间内不做
 *    Check、不打印、不分配（序列化缓冲与文档都在计时之外准备好）。
 *  - 参考值：与推导场景同类的"每字节"操作以场景预算目标作 reference，basis 取 `budget`
 *    （grading.md §2 E，POLICY 类；完整推导与全部假设见 measurement notes）。它锚定公开 JSON 库的 DOM 吞吐
 *    数据后打折，是**目标**：不是执行路径下界、不是硬件规格、也不是同轮实测对照。
 *  - 不适用预算的操作（find/遍历：没有场景假设、也没有同轮直接调用对照）basis 留空 → ungraded，
 *    实测值只作数量级记录（grading.md §2 E 的适用范围）。
 *  - 工具自身耗时（`ID` 的生成/解析/复制）不属于本域：本域只测插件自身的解析、序列化与访问成本。
 */

namespace
{
    using ID::Json::JsonDocument;
    using ID::Json::JsonError;
    using ID::Json::JsonValue;
    using ID::Json::JsonWriteOptions;
    using ID::Json::parse;
    using ID::Json::parse_non_owning;
    using ID::Json::serialize;
    using ID::Bench::FormatDouble;
    using ID::Bench::JsonFixture::MakeBulkDocument;
    using ID::Bench::JsonFixture::MakeConfigDocument;
    using ID::Bench::JsonFixture::MakeNestedDocument;
    using ID::Bench::JsonFixture::SampleDocument;

    constexpr int    kRounds          = 5;          // methodology §2：固定 5 轮，报中位数
    constexpr double kMinRoundMs      = 100.0;      // methodology §2：单轮时长下限
    constexpr double kTargetRoundMs   = 120.0;      // 标定目标：留 20% 余量，仍高于下限
    constexpr double kWarmupTargetMs  = 30.0;       // 预热时长（不计时）
    constexpr double kProbeIterations = 8.0;        // 标定 pass 的迭代数
    // 迭代数上限：只为"每次操作只有几纳秒"的访问类指标兜底，使 64M 上限 × 3 ns ≈ 192 ms 足以越过 100 ms 计时下限
    // （上限若只到 20M，3 ns × 20M ≈ 60 ms 会停在计时下限以下，违反 methodology §2）
    constexpr std::size_t kMaxIterations = 64'000'000;

    // ---- 场景预算目标（POLICY，basis=budget）----
    //
    // 锚定来源（用户授权检索的公开数据；平台/编译器/数据集与本机不同，不可直接横比，只作为"目标带宽"的锚）：
    //   来源 A：yyjson 官方 README 基准（twitter.json，DOM API，AWS EC2 AMD EPYC 7R32 / gcc 9.3）
    //           yyjson 常规 parse 1.72 GB/s、stringify 1.42 GB/s（insitu parse 1.80 / stringify 1.51）；
    //           同表 rapidjson utf8 0.26 GB/s、cJSON 0.32 GB/s、jansson 0.05 GB/s。
    //   来源 B：simdjson 官方 README / VLDB2019（Skylake，多数据集）simdjson DOM 2.4–3.5 GB/s、
    //           RapidJSON 0.6–0.8 GB/s、nlohmann 0.1–0.2 GB/s。
    //   来源 C：json_performance（M1 / clang 15）yyjson read 1107 MB/s、RapidJSON read 450 MB/s、nlohmann 80 MB/s。
    // IDJson 的形态是"DOM 急切物化 + 严格 RFC 8259 全校验 + arena 分配"，与 yyjson / RapidJSON 的 DOM 接口同类，
    // 与 simdjson on-demand（两阶段、按需解析）不同类，因此只用 DOM 同类的值作锚。
    //
    // 推导：1 B ÷ 1.72 GB/s = 0.5814 ns/B；目标 = 锚定值 × 4 = 2.3256 ns/B ≈ 2.33 ns/B（目标吞吐 ≈ 430 MB/s，
    //       约为 yyjson 锚定值的 25%）。序列化：1 B ÷ 1.42 GB/s = 0.7042 ns/B；× 4 = 2.8169 ns/B ≈ 2.82 ns/B。
    // 折扣理由（逐条）：① 平台/编译器/数据集不同（锚定是真机服务器 + 别的数据集），跨机器绝对值不可互换；
    //       ② IDJson 是全新实现、没有 SIMD 分词，对每个字符串/对象/数组都做 arena 分配与全量校验；
    //       ③ yyjson 的 insitu 模式复用输入缓冲，本库 owning 模式必须复制字符串（序列化不涉及此项）。
    constexpr double kParseAnchorBytesPerSecond     = 1.72e9;
    constexpr double kSerializeAnchorBytesPerSecond = 1.42e9;
    constexpr double kBudgetDivisor                 = 4.0;
    constexpr double kParseBudgetNsPerByte     = (1.0e9 / kParseAnchorBytesPerSecond) * kBudgetDivisor;
    constexpr double kSerializeBudgetNsPerByte = (1.0e9 / kSerializeAnchorBytesPerSecond) * kBudgetDivisor;

    // 计时之外的量测事实（固定文档规模、段申请次数、序列化输出字节），由 measurement notes 输出
    struct MeasurementFacts
    {
        std::size_t config_bytes;
        std::size_t bulk_bytes;
        std::size_t nested_bytes;
        std::size_t serialize_compact_bytes;
        std::size_t serialize_indent_bytes;
        std::size_t alloc_bulk_16k;
        std::size_t alloc_bulk_64k;
        std::size_t alloc_bulk_256k;
        std::size_t alloc_config_64k;
        std::size_t alloc_nested_64k;
        std::size_t bytes_bulk_16k;
        std::size_t bytes_bulk_64k;
        std::size_t bytes_bulk_256k;
        std::size_t find_linear_members;
        std::size_t find_indexed_members;
        std::size_t iterate_objects;
    };
    inline constinit MeasurementFacts g_facts{};

    struct Measurement
    {
        double      median_ns;
        double      minimum_ns;
        double      maximum_ns;
        double      spread;
        bool        is_unstable;
        std::size_t iterations;
    };

    /**
     *  @brief 预热 + 标定 + 5 轮计时的通用骨架
     *  @param operation 单次操作的 noexcept 可调用对象；结果必须在其中被真实消费
     *  @return 中位数/极值/极差/不稳定标记与实际使用的迭代数
     */
    template <typename Operation>
    Measurement MeasurePerOp(Operation& operation)
    {
        // 预热：至少 kWarmupTargetMs，不计时
        {
            const double warmup_start = ID::Bench::NowNs();
            while (ID::Bench::NowNs() - warmup_start < kWarmupTargetMs * 1.0e6)
            {
                operation();
            }
        }

        // 标定：按实测单次耗时反算迭代数，使单轮落在 kTargetRoundMs 附近
        const double probe_start = ID::Bench::NowNs();
        for (int index = 0; index < static_cast<int>(kProbeIterations); ++index)
        {
            operation();
        }
        double per_op_ns = (ID::Bench::NowNs() - probe_start) / kProbeIterations;
        if (!(per_op_ns > 0.0))
        {
            per_op_ns = 1.0;    // 计时分辨率不足时的防守：至少跑 1 次迭代
        }

        std::size_t iterations = static_cast<std::size_t>((kTargetRoundMs * 1.0e6) / per_op_ns);
        if (iterations < 1)
        {
            iterations = 1;
        }
        if (iterations > kMaxIterations)
        {
            iterations = kMaxIterations;
        }

        double samples[kRounds] = {};
        for (int round = 0; round < kRounds; ++round)
        {
            double elapsed_ns = 0.0;
            for (;;)
            {
                const ID::Bench::Timer timer;
                for (std::size_t index = 0; index < iterations; ++index)
                {
                    operation();
                }
                elapsed_ns = timer.ElapsedNs();

                // 单轮时长不足 methodology §2 的下限时翻倍迭代数重跑本轮（该次不计入样本），
                // 保证 5 个计入样本的轮次都落在 ≥ kMinRoundMs 的窗口里
                if (elapsed_ns >= kMinRoundMs * 1.0e6 || iterations >= kMaxIterations)
                {
                    break;
                }
                iterations = (iterations > kMaxIterations / 2) ? kMaxIterations : iterations * 2;
            }
            samples[round] = elapsed_ns / static_cast<double>(iterations);
        }

        const ID::Bench::SampleStats stats = ID::Bench::Summarize(samples, kRounds);

        Measurement result{};
        result.median_ns   = stats.median;
        result.minimum_ns  = stats.minimum;
        result.maximum_ns  = stats.maximum;
        result.spread      = stats.spread;
        result.is_unstable = stats.is_unstable;
        result.iterations  = iterations;
        return result;
    }

    std::string MeasurementNote(const Measurement& measurement, std::string_view tail)
    {
        const double round_ms =
            measurement.median_ns * static_cast<double>(measurement.iterations) / 1.0e6;
        std::string note = "iters=" + std::to_string(measurement.iterations);
        note += " rounds=" + std::to_string(kRounds);
        note += " 单轮≈" + FormatDouble(round_ms, 1) + " ms";
        note += " min=" + FormatDouble(measurement.minimum_ns, 2);
        note += " max=" + FormatDouble(measurement.maximum_ns, 2);
        note += " spread=" + FormatDouble(measurement.spread * 100.0, 1) + "%";
        note += tail;
        return note;
    }

    // 参考值按配置持有（grading.md §7）：本表的预算只对**优化后**构建成立。未定义 NDEBUG 的 Debug 变体里，
    // 解析/序列化被 -O0 的调试构建拖慢数倍，那些绝对 ns 不能代表交付性能，拿同一预算去评级只会得到误导性的
    // terrible/poor；因此 Debug 变体一律 basis 留空 → ungraded，只记录读数并注明原因。
#if defined(NDEBUG)
    constexpr bool kGradedConfiguration = true;
#else
    constexpr bool kGradedConfiguration = false;
#endif

    constexpr const char* kDebugConfigNote =
        "；本变体是非优化配置（NDEBUG 未定义，Debug），绝对读数不代表优化后的交付性能 → 不评级"
        "（参考值只按优化构建持有，grading.md §7 不允许跨配置借用），只作回归前的数量级记录";

    std::string BudgetTailForBytes(std::size_t bytes, double budget_ns_per_byte)
    {
        std::string tail = "；reference=" + std::to_string(bytes) + " B × " + FormatDouble(budget_ns_per_byte, 2)
                           + " ns/B = "
                           + FormatDouble(static_cast<double>(bytes) * budget_ns_per_byte, 2)
                           + " ns [budget]（场景预算目标 = POLICY，不是下界也不是同轮实测对照；"
                             "推导与全部假设见 measurement notes）";
        return tail;
    }

    /**
     *  @brief 记录一条"每次操作处理 N 字节"的吞吐指标
     *  @note 进程内极差超带时 basis 留空 → ungraded（grading.md §5/§6），不建议把它当有效评级
     */
    void RecordPerByteMetric(std::string_view name, const Measurement& measurement, std::size_t bytes,
                            double budget_ns_per_byte, std::string_view operation_note)
    {
        std::string tail = "；" + std::string(operation_note) + BudgetTailForBytes(bytes, budget_ns_per_byte);
        bool        is_graded = kGradedConfiguration;
        if (measurement.is_unstable)
        {
            tail += "；本项不给比值评级：进程内 5 轮极差超出 grading.md §5 的 20% 噪声带 → "
                    "basis 留空 → ungraded 并计入退出码，只做数量级比较";
            is_graded = false;
        }
        if (!kGradedConfiguration)
        {
            tail += kDebugConfigNote;
        }

        ID::Bench::RecordMetric({
            .name         = name,
            .actual_ns    = measurement.median_ns,
            .reference_ns = static_cast<double>(bytes) * budget_ns_per_byte,
            .basis        = is_graded ? std::string_view("budget") : std::string_view{},
            .note         = MeasurementNote(measurement, tail),
        });
    }

    // ==================== 各类操作的测量包装 ====================

    Measurement MeasureParse(const std::string& text, bool owning, std::size_t block_size, bool fresh_document)
    {
        std::uint64_t checksum = 0;
        Measurement   measurement{};

        if (fresh_document)
        {
            // 冷路径：每次迭代新建 JsonDocument（含段申请与析构归还），用于与"复用同一文档"对照
            auto operation = [&text, owning, block_size, &checksum]() {
                JsonDocument    temporary(block_size);
                const JsonError code = owning ? parse(temporary, text) : parse_non_owning(temporary, text);
                checksum += (code == JsonError::none) ? temporary.root().size() : 1;
            };
            measurement = MeasurePerOp(operation);
        }
        else
        {
            // 主场景：同一 JsonDocument 反复解析（parse 入口 reset，常规段复用）
            JsonDocument  document(block_size);
            auto operation = [&document, &text, owning, &checksum]() {
                const JsonError code = owning ? parse(document, text) : parse_non_owning(document, text);
                checksum += (code == JsonError::none) ? document.root().size() : 1;
            };
            measurement = MeasurePerOp(operation);
        }

        ID::Bench::KeepAlive(&checksum);
        return measurement;
    }

    struct SerializeMeasurement
    {
        Measurement measurement;
        std::size_t output_bytes;
        bool        ok;
    };

    SerializeMeasurement MeasureSerialize(const JsonValue& root, std::uint32_t indent)
    {
        JsonWriteOptions options{};
        options.indent = indent;

        SerializeMeasurement result{};
        result.output_bytes = serialize(root, nullptr, 0, options);
        if (result.output_bytes == 0)
        {
            result.ok = false;
            return result;
        }

        std::vector<char> buffer(result.output_bytes + 1, 0);
        std::uint64_t     checksum = 0;
        auto operation = [&root, &options, &buffer, &checksum]() {
            checksum += serialize(root, buffer.data(), buffer.size(), options);
        };
        result.measurement = MeasurePerOp(operation);
        ID::Bench::KeepAlive(&checksum);

        // 计时之外核对：一次完整序列化的返回值等于探测长度
        result.ok = serialize(root, buffer.data(), buffer.size(), options) == result.output_bytes;
        return result;
    }

    Measurement MeasureFind(const JsonValue& object, std::string_view key, std::uint64_t& checksum)
    {
        auto operation = [&object, key, &checksum]() {
            const JsonValue* found = object.find(key);
            checksum += (found != nullptr) ? 1 : 0;
        };
        return MeasurePerOp(operation);
    }

    Measurement MeasureIterate(const JsonValue& array, std::uint64_t& checksum)
    {
        auto operation = [&array, &checksum]() {
            std::int64_t sum = 0;
            for (const JsonValue* item = array.begin(); item != array.end(); ++item)
            {
                const JsonValue* id = item->find("id");
                if (id != nullptr && id->is_int())
                {
                    sum += id->as_int64();
                }
            }
            checksum += static_cast<std::uint64_t>(sum);
        };
        return MeasurePerOp(operation);
    }

    /**
     *  @brief 计时之外统计"一次解析的段申请次数与申请字节数"
     *  @note 计数分配器会写进程级计数，因此单独跑一趟、不进入计时区间
     */
    std::size_t CountSegments(const std::string& text, std::size_t block_size, bool owning, std::size_t& out_bytes)
    {
        ID::Bench::JsonFixture::ResetSegmentCounters();
        {
            JsonDocument document(block_size, ID::Bench::JsonFixture::CountingSegmentAlloc,
                                  ID::Bench::JsonFixture::CountingSegmentFree);
            const JsonError code = owning ? parse(document, text) : parse_non_owning(document, text);
            if (code != JsonError::none)
            {
                ID::Bench::Check(false, "perf.count_segments_parse_failed",
                                 "统计段申请时解析失败：段申请次数不可用于口径说明");
            }
        }
        out_bytes = ID::Bench::JsonFixture::g_segment_counters.alloc_bytes;
        const std::size_t calls = ID::Bench::JsonFixture::g_segment_counters.alloc_calls;
        ID::Bench::JsonFixture::ResetSegmentCounters();
        return calls;
    }

    /// @brief 对象成员数固定的测试对象（用于 find 的线性/哈希两条路径）
    std::string MakeObjectDocument(std::size_t member_count)
    {
        std::string text;
        text += "{";
        for (std::size_t index = 0; index < member_count; ++index)
        {
            if (index != 0)
            {
                text += ",";
            }
            text += "\"k" + std::to_string(index) + "\":" + std::to_string(index);
        }
        text += "}";
        return text;
    }
} // namespace

namespace ID::Bench
{
    void IdJsonPerformanceTest() noexcept
    {
        // ---- 固定输入（构造在计时之外）----
        const SampleDocument config = MakeConfigDocument();
        const SampleDocument bulk   = MakeBulkDocument();
        const SampleDocument nested = MakeNestedDocument();

        g_facts.config_bytes = config.bytes;
        g_facts.bulk_bytes   = bulk.bytes;
        g_facts.nested_bytes = nested.bytes;

        // 段申请次数（计时之外的另一趟；口径见 measurement notes）
        g_facts.alloc_bulk_16k  = CountSegments(bulk.text, 16 * 1024, true, g_facts.bytes_bulk_16k);
        g_facts.alloc_bulk_64k  = CountSegments(bulk.text, 64 * 1024, true, g_facts.bytes_bulk_64k);
        g_facts.alloc_bulk_256k = CountSegments(bulk.text, 256 * 1024, true, g_facts.bytes_bulk_256k);
        std::size_t scratch_bytes = 0;
        g_facts.alloc_config_64k  = CountSegments(config.text, ID::Json::kDefaultBlockSize, true, scratch_bytes);
        g_facts.alloc_nested_64k  = CountSegments(nested.text, ID::Json::kDefaultBlockSize, true, scratch_bytes);

        // ---- 前置不变量（性能区的 Check，与指标表分开输出）----
        {
            JsonDocument document;
            Check(parse(document, config.text) == JsonError::none, "perf.input.config_parses",
                  "配置类文档必须可解析（否则下方吞吐无意义）");
            Check(parse(document, bulk.text) == JsonError::none, "perf.input.bulk_parses",
                  "大对象数组文档必须可解析");
            Check(parse(document, nested.text) == JsonError::none, "perf.input.nested_parses",
                  "深嵌套文档必须可解析");
            Check(document.arena().block_count() > 0 && document.arena().capacity() > 0,
                  "perf.input.arena_used", "解析确实使用了 arena（否则测的不是物化路径）");
        }

        // ---- 1. 解析吞吐：owning（主场景 = 同一文档反复解析，常规段复用）----
        const Measurement parse_config_owning =
            MeasureParse(config.text, true, ID::Json::kDefaultBlockSize, false);
        RecordPerByteMetric("idjson.parse.config_owning", parse_config_owning, config.bytes, kParseBudgetNsPerByte,
                            "配置类中小文档 ~1.6 KB、owning（字符串深拷贝进 arena）、64 KiB 段，同一文档反复解析");

        const Measurement parse_bulk_owning = MeasureParse(bulk.text, true, ID::Json::kDefaultBlockSize, false);
        RecordPerByteMetric("idjson.parse.bulk_owning", parse_bulk_owning, bulk.bytes, kParseBudgetNsPerByte,
                            "800 对象混合数组、owning、64 KiB 段，同一文档反复解析（arena 复用主场景）");

        const Measurement parse_nested_owning = MeasureParse(nested.text, true, ID::Json::kDefaultBlockSize, false);
        RecordPerByteMetric("idjson.parse.nested_owning", parse_nested_owning, nested.bytes, kParseBudgetNsPerByte,
                            "96 层嵌套 + 两个 8 KB 长串、owning、64 KiB 段");

        // ---- 2. 解析吞吐：non_owning（无转义长串零拷贝）----
        const Measurement parse_config_non_owning =
            MeasureParse(config.text, false, ID::Json::kDefaultBlockSize, false);
        RecordPerByteMetric("idjson.parse.config_non_owning", parse_config_non_owning, config.bytes,
                            kParseBudgetNsPerByte,
                            "配置类中小文档、non_owning（≤7 字节内联、>7 字节无转义串零拷贝）");

        const Measurement parse_bulk_non_owning = MeasureParse(bulk.text, false, ID::Json::kDefaultBlockSize, false);
        RecordPerByteMetric("idjson.parse.bulk_non_owning", parse_bulk_non_owning, bulk.bytes,
                            kParseBudgetNsPerByte, "800 对象混合数组、non_owning（标签与描述零拷贝）");

        const Measurement parse_nested_non_owning =
            MeasureParse(nested.text, false, ID::Json::kDefaultBlockSize, false);
        RecordPerByteMetric("idjson.parse.nested_non_owning", parse_nested_non_owning, nested.bytes,
                            kParseBudgetNsPerByte, "96 层嵌套 + 两个 8 KB 长串、non_owning（长串引用输入）");

        // ---- 3. 文档生命周期对照：每次新建 JsonDocument（冷路径）----
        const Measurement parse_bulk_fresh = MeasureParse(bulk.text, true, ID::Json::kDefaultBlockSize, true);
        RecordPerByteMetric("idjson.parse.fresh_document_bulk", parse_bulk_fresh, bulk.bytes, kParseBudgetNsPerByte,
                            "同一文档内容，但每次迭代新建并析构 JsonDocument（含段申请/归还）；"
                            "与 bulk_owning 的差额即段复用省下的成本。该项的进程内极差通常最大："
                            "每轮都要 malloc/free 数百 KB 段，波动来自分配器与页复用，"
                            "因此本项多半按 unstable 处理（basis 留空 → ungraded），只作数量级对照");

        // ---- 4. 序列化吞吐（同一批文档：800 对象混合数组）----
        JsonDocument serialize_document;
        Check(parse(serialize_document, bulk.text) == JsonError::none, "perf.serialize_source_parses",
              "序列化的输入文档解析成功");
        const JsonValue& serialize_root = serialize_document.root();

        const SerializeMeasurement serialize_compact = MeasureSerialize(serialize_root, 0);
        g_facts.serialize_compact_bytes = serialize_compact.output_bytes;
        Check(serialize_compact.ok, "perf.serialize.compact_length_matches",
              "紧凑序列化返回值等于探测长度（一次遍历同时得到内容与所需字节数）");
        RecordPerByteMetric("idjson.serialize.compact_bulk", serialize_compact.measurement,
                            serialize_compact.output_bytes, kSerializeBudgetNsPerByte,
                            "紧凑输出、输出缓冲在计时之外按探测长度预分配");

        const SerializeMeasurement serialize_indent = MeasureSerialize(serialize_root, 2);
        g_facts.serialize_indent_bytes = serialize_indent.output_bytes;
        Check(serialize_indent.ok, "perf.serialize.indent_length_matches",
              "缩进序列化返回值等于探测长度（缩进只增加空白，不改变语义）");
        RecordPerByteMetric("idjson.serialize.indent_bulk", serialize_indent.measurement,
                            serialize_indent.output_bytes, kSerializeBudgetNsPerByte,
                            "indent=2 输出；缩进用 memset 填空格，输出缓冲在计时之外预分配");

        // ---- 5. 访问成本（无场景假设与同轮对照 → ungraded，只记录数量级）----
        std::string small_object_text = MakeObjectDocument(8);
        std::string indexed_object_text = MakeObjectDocument(32);
        g_facts.find_linear_members  = 8;
        g_facts.find_indexed_members = 32;

        JsonDocument small_object_document;
        JsonDocument indexed_object_document;
        Check(parse(small_object_document, small_object_text) == JsonError::none
                  && parse(indexed_object_document, indexed_object_text) == JsonError::none,
              "perf.access.fixtures_parse", "find 用的 8/32 成员对象解析成功");
        Check(small_object_document.root().find("k7") != nullptr
                  && indexed_object_document.root().find("k31") != nullptr,
              "perf.access.fixture_hits", "命中断言的前置：两个键都真实存在");

        std::uint64_t access_checksum = 0;
        const Measurement find_linear = MeasureFind(small_object_document.root(), "k7", access_checksum);
        const Measurement find_indexed = MeasureFind(indexed_object_document.root(), "k31", access_checksum);
        const Measurement find_miss = MeasureFind(indexed_object_document.root(), "missing-key", access_checksum);

        JsonDocument iterate_document;
        Check(parse(iterate_document, bulk.text) == JsonError::none, "perf.access.iterate_source_parses",
              "遍历用例的输入文档解析成功");
        g_facts.iterate_objects = iterate_document.root().size();
        const Measurement iterate_bulk = MeasureIterate(iterate_document.root(), access_checksum);
        ID::Bench::KeepAlive(&access_checksum);

        const std::string_view access_basis{};
        RecordMetric({
            .name         = "idjson.access.find_hit_linear",
            .actual_ns    = find_linear.median_ns,
            .reference_ns = 0.0,
            .basis        = access_basis,
            .note         = MeasurementNote(find_linear,
                                            "；8 成员对象（成员数 ≤ kObjectIndexThreshold=8）命中末键，走线性扫描；"
                                            "本项 basis 留空 → ungraded：find 不属于“每字节”吞吐，"
                                            "且没有同轮、同进程的直接调用对照（grading.md §2 E 适用范围）"),
        });
        RecordMetric({
            .name         = "idjson.access.find_hit_indexed",
            .actual_ns    = find_indexed.median_ns,
            .reference_ns = 0.0,
            .basis        = access_basis,
            .note         = MeasurementNote(find_indexed,
                                            "；32 成员对象（成员数 > 8）命中末键，走 FNV-1a 开放寻址索引；"
                                            "本项 basis 留空 → ungraded：无同轮直接调用对照，不借用吞吐预算"),
        });
        RecordMetric({
            .name         = "idjson.access.find_miss_indexed",
            .actual_ns    = find_miss.median_ns,
            .reference_ns = 0.0,
            .basis        = access_basis,
            .note         = MeasurementNote(find_miss,
                                            "；32 成员对象查询不存在的键（探测到空桶后返回 nullptr）；"
                                            "本项 basis 留空 → ungraded：无同轮直接调用对照，不借用吞吐预算"),
        });
        RecordMetric({
            .name         = "idjson.access.iterate_find_bulk",
            .actual_ns    = iterate_bulk.median_ns,
            .reference_ns = 0.0,
            .basis        = access_basis,
            .note         = MeasurementNote(iterate_bulk,
                                            "；每次操作 = 遍历 800 个对象并对每个对象 find(\"id\") 一次（含下标推进）；"
                                            "本项 basis 留空 → ungraded：整轮遍历不是“每字节”操作，"
                                            "也没有同轮对照实现"),
        });

        // ---- 6. 段大小敏感性（同一负载、三档段大小）----
        const Measurement parse_bulk_16k = MeasureParse(bulk.text, true, 16 * 1024, false);
        RecordPerByteMetric("idjson.parse.blocksize_16k_bulk", parse_bulk_16k, bulk.bytes, kParseBudgetNsPerByte,
                            "同一 800 对象文档、owning、段大小 16 KiB（段更小 → 段申请与碎片更多）");

        const Measurement parse_bulk_256k = MeasureParse(bulk.text, true, 256 * 1024, false);
        RecordPerByteMetric("idjson.parse.blocksize_256k_bulk", parse_bulk_256k, bulk.bytes, kParseBudgetNsPerByte,
                            "同一 800 对象文档、owning、段大小 256 KiB（段更大 → 单次申请更贵、内部碎片更多）");

        // 显式消费一次字符串对象，避免"构造后未使用"类诊断掩盖真实用法
        ID::Bench::KeepAlive(&small_object_text);
        ID::Bench::KeepAlive(&indexed_object_text);
    }

    void IdJsonPerformanceNotes() noexcept
    {
        // ---- 统计口径 ----
        ReportLine("    - 统计口径（多轮）：预热 ≥30 ms 不计时；标定 pass 按实测单次耗时反算迭代数，"
                   "使单轮≈120 ms（methodology §2 要求 ≥100 ms）；固定 5 轮计时，报**中位数**，"
                   "每项 note 给出 iters / 单轮 ms / min / max / spread。");
        ReportLine("    - 归一化：解析按**输入字节**归一（ns/字节 → 换算成『每次解析该文档』的 ns），"
                   "序列化按**输出字节**归一；两者都是 O(n) 指标，因此只对同一操作用一份每字节预算，"
                   "不以单点比值代表复杂度（grading.md §6）。");
        ReportLine("    - 防优化：每轮把解析结果的 size()/序列化写入字节数/find 命中数累加进 checksum，"
                   "循环外经 KeepAlive 写入跨 TU sink；计时区间内不做 Check、不打印、不分配。");
        ReportLine("    - 段复用口径：`*.bulk_owning` 等指标在同一 JsonDocument 上反复 parse"
                   "（parse 入口 reset，常规段复用）；`fresh_document_bulk` 每次迭代新建并析构文档，"
                   "两者差额即段申请/归还的成本量级。");

        // ---- 参考值推导（budget = POLICY）----
        ReportLine("    - reference 推导（解析，basis=budget，POLICY 类；不是下界、不是同轮实测对照）：");
        ReportLine("      锚定来源 A（yyjson 官方 README 基准，twitter.json，DOM API，AWS EC2 AMD EPYC 7R32 / gcc 9.3）："
                   "yyjson 常规 parse 1.72 GB/s（insitu 1.80 GB/s）；同表 rapidjson utf8 0.26 GB/s、cJSON 0.32 GB/s。"
                   "来源 B（simdjson VLDB2019，Skylake）RapidJSON 0.6–0.8 GB/s、nlohmann 0.1–0.2 GB/s；"
                   "来源 C（json_performance，M1/clang 15）yyjson read 1107 MB/s、RapidJSON read 450 MB/s。");
        ReportLine("      IDJson 属“DOM 急切物化 + 严格 RFC 8259 全校验 + arena 分配”，与 yyjson/RapidJSON 的 DOM 接口同类，"
                   "与 simdjson on-demand（两阶段、按需解析）不同类 → 只用 DOM 同类值作锚，不引用 on-demand 与 SIMD 读数。");
        ReportLine(std::string("      推导：1 B ÷ 1.72 GB/s = ") + FormatDouble(1.0e9 / kParseAnchorBytesPerSecond, 4)
                   + " ns/B；目标 = 锚定值 × 4 = " + FormatDouble(kParseBudgetNsPerByte, 4)
                   + " ns/B（目标吞吐 ≈ "
                   + FormatDouble(1.0e9 / kParseBudgetNsPerByte / 1.0e6, 0) + " MB/s，约为锚定值的 25%）。");
        ReportLine(std::string("      序列化：锚定来源 A 的 stringify 1.42 GB/s（insitu 1.51 GB/s）；1 B ÷ 1.42 GB/s = ")
                   + FormatDouble(1.0e9 / kSerializeAnchorBytesPerSecond, 4) + " ns/B；× 4 = "
                   + FormatDouble(kSerializeBudgetNsPerByte, 4) + " ns/B。");
        ReportLine("      折扣理由（逐条）：① 平台/编译器/数据集不同（锚定是真机服务器与别的数据集），"
                   "跨机器绝对值不可互换；② IDJson 是全新实现、没有 SIMD 分词，对每个字符串/对象/数组都做"
                   "arena 分配与全量校验；③ yyjson insitu 复用输入缓冲，本库 owning 必须复制字符串"
                   "（序列化不涉及 ③）。");
        ReportLine(std::string("      假设（缺一即不成立）：目标解析吞吐 ≥ 430 MB/s、目标序列化吞吐 ≥ 355 MB/s；"
                               "负载为配置类（~1.6 KB）/对象数组类（~150 KB）/嵌套类（~25 KB）三类合成文档；"
                               "构建身份见报告头 compiler/build/plugin 行（本报告 config=")
                   + BENCHMARK_BUILD_CONFIG + " / arch=" + BENCHMARK_ARCH + " / LINK=" + BENCHMARK_LINK_MODE
                   + " / IDJson.log_bridge=" + BENCHMARK_IDJSON_LOG + "）。");
        ReportLine("      局限：budget 是工程目标（POLICY），不是理论下界、不是硬件规格、也不是同轮对照；"
                   "比值 < 0.5 只表示离目标还有余量，不触发“参考值过松”复核（该复核只适用于 theoretical）；"
                   "跨机器/跨编译器/跨配置的读数与本表的比值都不可互换，公开数据仅用于确定目标带宽。");

        ReportLine("      配置门槛：只有定义了 NDEBUG 的优化构建才用本表预算评级；Debug 变体的全部吞吐指标 basis 留空 → "
                   "ungraded（参考值按构建配置各自持有，跨配置借用等于把不可比的读数当结论）。");

        // ---- 本次运行的量测事实 ----
        ReportLine(std::string("    - 固定输入规模（计时之外测量）：config=") + std::to_string(g_facts.config_bytes)
                   + " B；bulk=" + std::to_string(g_facts.bulk_bytes) + " B；nested="
                   + std::to_string(g_facts.nested_bytes) + " B；序列化输出 compact="
                   + std::to_string(g_facts.serialize_compact_bytes) + " B / indent=2 "
                   + std::to_string(g_facts.serialize_indent_bytes) + " B。");
        ReportLine(std::string("    - 段申请次数（计数分配器，单独一趟、不计时）：bulk owning 在 16 KiB 段下 ")
                   + std::to_string(g_facts.alloc_bulk_16k) + " 次 / "
                   + std::to_string(g_facts.bytes_bulk_16k) + " B；64 KiB 段下 "
                   + std::to_string(g_facts.alloc_bulk_64k) + " 次 / " + std::to_string(g_facts.bytes_bulk_64k)
                   + " B；256 KiB 段下 " + std::to_string(g_facts.alloc_bulk_256k) + " 次 / "
                   + std::to_string(g_facts.bytes_bulk_256k) + " B；config 64 KiB 段下 "
                   + std::to_string(g_facts.alloc_config_64k) + " 次；nested 64 KiB 段下 "
                   + std::to_string(g_facts.alloc_nested_64k) + " 次。");
        ReportLine(std::string("    - 访问成本对象规模：find 线性路径 8 成员、哈希路径/未命中 32 成员"
                               "（kObjectIndexThreshold=8 之上才建索引）；遍历用例每次遍历 ")
                   + std::to_string(g_facts.iterate_objects) + " 个对象。");

        // ---- 未列入指标 ----
        ReportLine("    - 未列入指标（methodology §7 允许单独说明，不混入热路径读数）：JsonDocument 构造/析构成本"
                   "（只在 fresh_document_bulk 里间接体现）、attach/detach_error_logger 的挂接成本、"
                   "解析失败的冷路径成本（行/列统计是 O(已读长度)）、arena 巨段分配的单次成本。");
        ReportLine("    - 工具耗时口径：本域只测 IDJson 插件自身的解析/序列化/访问成本；`ID` 工具的生成、"
                   "依赖解析与资产复制耗时不属于插件性能，未计入本表。");
        ReportLine("    - 接入成本口径：本域按“公开头 + CMake 目标”直接链接静态库 IDJson，"
                   "不存在运行期服务槽或注册表；owning/non_owning 都是同一目标的直接调用，"
                   "因此没有单独的接入成本指标（相对 IDLog 的 DLL 边界，静态库没有跨模块调用层）。");

        // ---- 未验证 ----
        ReportLine("    - 未验证组合：MSVC Debug/32 位已产出 provisional 正确性归档 376/0/4/3"
                   "（其中 2 项是 IDLog 域 fatal 子进程超时失败，归 IDLog 域；本报告只覆盖报告头 build 行所示的工具链/配置）；"
                   "仍为未验证：Linux/clang、MSVC Release、SHARED 形态的 IDJson（当前交付形态是 STATIC）、32 位 GCC 运行期。");
        ReportLine("    - 跨进程资格（grading.md §5）：±20% 噪声带要求在**该机器/该配置**上连续 5 次运行并记录逐指标极差，"
                   "framework.md §4 说明该自动化尚未进入代码；本报告是一次运行的读数，进程内极差已随每项 note 给出，"
                   "但**不等于**跨进程资格已确认。");
        ReportLine("    - 并发不做性能结论：serialize 的线程安全由正确性域的并发用例取证（行为证据），"
                   "本域不做多线程吞吐测量，也不宣称任何扩展性。");
        ReportLine("    - 跨进程证据位置：本变体目录 `repeat-runs/repeat-runs.txt` 记录了同机同构建连续 5 次运行的"
                   "逐指标极差（spread=(max-min)/median）；进程内极差超带的项仍按 grading.md §5 不评级"
                   "（basis 留空 → ungraded），跨进程极差在带内也不用于把该项恢复评级。");
    }
} // namespace ID::Bench
