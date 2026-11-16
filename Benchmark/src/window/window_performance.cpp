#include "window/window_domain.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

#include "Core/Core.hpp"

#include "IDWindow.hpp"
#include "IDWindow/Event/EventData.hpp"
#include "IDWindow/KeyCode/KeyCodes.hpp"
#include "IDWindow/NativeWindowInfo.hpp"
#include "IDWindow/Window.hpp"
#include "IDWindow/WindowPool.hpp"
#include "IDWindow/WindowStatus.hpp"

#include "benchmark_support.hpp"

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

// 接入成本对照实现：直接调用同一份 vendor GLFW 的等价接线（os-baseline）。
// 只在 STATIC 形态成立：SHARED 形态下 GLFW 被静态编入 IDWindow.dll，测试侧再链一份静态 GLFW 会得到
// 第二份未初始化的 GLFW 全局状态（窗口类已注册、glfwInit 未在本副本上执行），因此该形态不做直接对照，
// 相关指标 basis 留空 → ungraded，并在 note 与 measurement notes 里写明原因。
#if defined(ID_WINDOW_STATIC)
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
    #define IDWINDOW_BENCH_HAS_GLFW_CONTROL 1
#else
    #define IDWINDOW_BENCH_HAS_GLFW_CONTROL 0
#endif

/**
 *  IDWindow 接入成本与转发路径成本用例。
 *
 *  口径（benchmark/grading/methodology）：
 *  - 每项指标 = 预热（不计时）+ 5 轮时间盒计时（每轮 ≥ 130 ms），报**中位数**，最小/最大/极差写进 note；
 *    极差 > 20% 标 unstable，按 grading.md §6 不当作有效评级（basis 留空 → ungraded 并计入退出码）。
 *  - 固定输入：同一进程内两个同尺寸（320x240）隐藏窗口——被测窗口与对照窗口；查询的是同一份平台属性；
 *    迭代数由时间盒决定（不预先写死），实际单轮迭代数写进 note。
 *  - 防优化：每轮把累加值 XOR 进 TU 私有累加器并 `KeepAlive` 到跨 TU sink，调用点在计时区间之外。
 *  - 参考值一律 `os-baseline`：同轮、同进程、相邻位置测得的"直接 GLFW 等价接线"。目标倍数 1.0，
 *    理由逐条写在 note（本插件是薄封装：一次成员判空 + 一次平台调用 + 构造返回值）。
 *  - 插件自身成本与 OS/driver 成本分开陈述：`get_size` 额外给出三级阶梯（Win32 `GetClientRect` →
 *    GLFW 直接调用 → 插件转发），阶梯差值写在 measurement notes，不参与评级。
 *  - 事件派发与初始化/清理成本本轮**未列入指标**：前者的插件侧成本被 OS 窗口消息成本淹没，
 *    后者不属于逐次转发路径；两者按 `Uncovered` 登记并写明障碍（benchmark/SKILL.md §10）。
 */

// ---- 分配计数探针：转发路径不得在插件侧分配 ----
//
// 只在 `g_allocation_probe_active` 为 true 的窗口内计数，窗口外每次分配只多一次 relaxed 读。
// `operator new` 的签名必须是标准可替换签名（不能加 noexcept），因此失败分支抛 bad_alloc：
// 这是替换函数的语言要求，不是本仓库业务代码的异常风格。
// 覆盖范围：本可执行文件与被静态链入其中的代码（IDWindow STATIC 形态 + vendor GLFW）；
// 其它模块（IDLog.dll 等）用自己的 CRT 实例分配，不经本探针，因此 SHARED 形态记 Uncovered。

namespace
{
    std::atomic<bool>          g_allocation_probe_active{ false };
    std::atomic<std::uint64_t> g_allocation_count{ 0 };
} // namespace

void* operator new(std::size_t size)
{
    if (g_allocation_probe_active.load(std::memory_order_relaxed))
    {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }

    void* const pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr)
    {
        throw std::bad_alloc();
    }
    return pointer;
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace
{
    namespace W = ID::Window;

    using ID::Bench::Check;
    using ID::Bench::FormatDouble;
    using ID::Bench::KeepAlive;
    using ID::Bench::NowNs;
    using ID::Bench::RecordMetric;
    using ID::Bench::ReportLine;
    using ID::Bench::SampleStats;
    using ID::Bench::Summarize;
    using ID::Bench::Uncovered;

    constexpr int     kRounds        = 5;
    constexpr double  kRoundTargetNs = 130.0e6;     // 每轮约 130 ms（methodology §2 的单轮 ≥100 ms 留余量）
    constexpr int     kBatch         = 64;          // 时间盒判定粒度：每 64 次迭代读一次计时源
    constexpr std::size_t kAllocationProbeIters = 200'000;

    constinit std::uint64_t g_checksum = 0;

    struct Round
    {
        double        m_ns_per_op  = 0.0;
        std::uint64_t m_iterations = 0;
    };

    /**
     *  @brief 时间盒计时一轮：循环到经过时间达到目标，按实际迭代数折算 ns/op
     *  @note 计时源读取每 kBatch 次迭代一次，摊到每次操作 < 1 ns（见 measurement notes）
     */
    template <typename Op>
    Round measure_round(Op& op, double target_ns) noexcept
    {
        Round round{};

        const double  begin    = NowNs();
        const double  deadline = begin + target_ns;
        std::uint64_t checksum = 0;
        std::uint64_t count    = 0;

        do
        {
            for (int index = 0; index < kBatch; ++index)
            {
                checksum += op();
            }
            count += kBatch;
        } while (NowNs() < deadline);

        const double end = NowNs();

        round.m_ns_per_op  = (end - begin) / static_cast<double>(count);
        round.m_iterations = count;

        g_checksum ^= checksum;
        KeepAlive(&g_checksum);
        return round;
    }

    struct Measurement
    {
        double        m_median     = 0.0;
        double        m_minimum    = 0.0;
        double        m_maximum    = 0.0;
        double        m_spread     = 0.0;
        bool          m_unstable   = false;
        std::uint64_t m_iterations = 0;
        double        m_round_ms   = 0.0;
    };

    template <typename Op>
    Measurement measure(Op& op) noexcept
    {
        Measurement result{};

        // 预热：半轮，不计时（把首次调用的冷状态与频率爬升走掉）
        (void)measure_round(op, kRoundTargetNs * 0.5);

        double samples[kRounds] = {};
        for (int round = 0; round < kRounds; ++round)
        {
            const Round measured = measure_round(op, kRoundTargetNs);
            samples[round]       = measured.m_ns_per_op;
            result.m_iterations  = measured.m_iterations;
            result.m_round_ms    = (measured.m_ns_per_op * static_cast<double>(measured.m_iterations)) / 1.0e6;
        }

        const SampleStats stats = Summarize(samples, kRounds);

        result.m_median   = stats.median;
        result.m_minimum  = stats.minimum;
        result.m_maximum  = stats.maximum;
        result.m_spread   = stats.spread;
        result.m_unstable = stats.is_unstable;
        return result;
    }

    /**
     *  @brief 统一拼装 5 轮统计尾巴：中位/最小/最大/极差/单轮迭代数/单轮时长
     */
    std::string rounds_tail(const Measurement& measurement) noexcept
    {
        std::string tail = "；5 轮中位数，min=";
        tail += FormatDouble(measurement.m_minimum, 2);
        tail += " max=";
        tail += FormatDouble(measurement.m_maximum, 2);
        tail += " spread=";
        tail += FormatDouble(measurement.m_spread * 100.0, 1);
        tail += "% 单轮≈";
        tail += FormatDouble(measurement.m_round_ms, 0);
        tail += " ms（";
        tail += std::to_string(measurement.m_iterations);
        tail += " 次/轮）";
        return tail;
    }

    // ---- 事件派发计数：只用来证明「确实派发到了该窗口」 ----
    constinit std::uint32_t g_callback_hits = 0;

    // ---- get_size 的三级阶梯读数：只用于把插件成本与 OS/driver 成本分开陈述，不参与评级 ----
    constinit double g_ladder_os_ns     = 0.0;
    constinit double g_ladder_glfw_ns   = 0.0;
    constinit double g_ladder_plugin_ns = 0.0;
    constinit bool   g_ladder_has_os    = false;
    constinit bool   g_ladder_has_glfw  = false;

    void on_window_size(void* user, W::Window&, const W::Event::WindowSizeEvent&) noexcept
    {
        (void)user;
        ++g_callback_hits;
    }

    // 对照实现：一次非内联调用 + 一次成员读，用来给「纯取值」指标一个同形状的 floor
    constinit W::WindowUINT g_control_id = 0;       // TU 私有状态：只被下面的对照函数读取

    ID_NOINLINE W::WindowUINT control_return_id() noexcept
    {
        return g_control_id;
    }

    struct Setup
    {
        bool     m_ok           = false;
        W::Window* m_window     = nullptr;
        W::WindowHandle m_handle{};
#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
        GLFWwindow* m_control   = nullptr;
#endif
    };

    Setup build_windows() noexcept
    {
        Setup setup{};

        W::shutdown();
        if (!W::init())
        {
            Check(false, "window.perf.init", "前置失败：ID::Window::init() 返回 false");
            return setup;
        }

        W::WindowDesc desc{};
        desc.title      = "bench-perf";
        desc.size       = W::WindowSize{ 320, 240 };
        desc.is_visible = false;
        desc.render_api = W::RenderApi::None;

        const W::WindowHandle handle = W::WindowPool::create_window(desc);
        if (!handle.is_valid())
        {
            Check(false, "window.perf.create_window", "前置失败：被测窗口创建失败");
            W::shutdown();
            return setup;
        }

        setup.m_handle = handle;
        setup.m_window = &W::WindowPool::get_window(handle);

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
        ::glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        setup.m_control = ::glfwCreateWindow(320, 240, "bench-perf-control", nullptr, nullptr);
        Check(setup.m_control != nullptr, "window.perf.control_window_created",
              "对照实现需要同一进程内同尺寸的隐藏 GLFW 窗口（与插件共用同一份 GLFW 全局状态）");
        if (setup.m_control == nullptr)
        {
            W::WindowPool::destroy_window(handle);
            W::shutdown();
            return setup;
        }

        int control_width  = 0;
        int control_height = 0;
        ::glfwGetWindowSize(setup.m_control, &control_width, &control_height);
        const W::WindowSize plugin_size = setup.m_window->get_size();
        Check(static_cast<std::uint32_t>(control_width) == plugin_size.width
                  && static_cast<std::uint32_t>(control_height) == plugin_size.height,
              "window.perf.control_same_size",
              std::string("对照窗口与被测窗口必须同尺寸，否则差值里会混进平台侧的尺寸处理成本；control=")
                  + std::to_string(control_width) + "x" + std::to_string(control_height)
                  + " plugin=" + std::to_string(plugin_size.width) + "x" + std::to_string(plugin_size.height));
#endif

        setup.m_ok = true;
        return setup;
    }

    void release_windows(Setup& setup) noexcept
    {
#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
        if (setup.m_control != nullptr)
        {
            ::glfwDestroyWindow(setup.m_control);
            setup.m_control = nullptr;
        }
#endif
        if (setup.m_handle.is_valid())
        {
            W::WindowPool::destroy_window(setup.m_handle);
            setup.m_handle = W::WindowHandle{};
        }
        W::shutdown();
    }

    // ---- 每条指标的公共尾巴：参考值口径 ----
    [[maybe_unused]] constexpr const char* kBaselineBasis = "os-baseline";
    [[maybe_unused]] constexpr const char* kBaselineReason =
        "同轮同进程直接 GLFW 等价接线对照（同一份 vendor GLFW 3.5.1、同尺寸隐藏窗口、相邻位置测量）；"
        "目标倍数 1.0，理由：被测入口是薄封装（一次成员/句柄判空 + 一次平台调用 + 构造返回值），"
        "转发路径不应引入超过一倍平台调用成本，倍数不放宽、不预留经验余量";

    [[maybe_unused]] constexpr const char* kNoControlNote =
        "；本变体无直接对照：SHARED 形态下 GLFW 静态编入 IDWindow.dll，测试侧链入的第二份静态 GLFW 状态"
        "无法与插件共享（未在本副本上 glfwInit），因此 reference 不可得 → basis 留空 → ungraded 并计入退出码"
        "（grading.md §4：宁可 ungraded，也不编参考值）；STATIC 变体有分级读数";
} // namespace

namespace ID::Bench
{
    void IdWindowPerformanceTest() noexcept
    {
        Setup setup = build_windows();
        if (!setup.m_ok)
        {
            return;
        }

        W::Window& window = *setup.m_window;

        // ================= 1) get_size：插件转发 vs 直接 GLFW =================

        {
            auto op_plugin = [&window]() noexcept -> std::uint64_t {
                const W::WindowSize size = window.get_size();
                return static_cast<std::uint64_t>(size.width) + static_cast<std::uint64_t>(size.height);
            };
            const Measurement plugin = measure(op_plugin);
            g_ladder_plugin_ns = plugin.m_median;

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
            GLFWwindow* const control = setup.m_control;
            auto op_control = [control]() noexcept -> std::uint64_t {
                int width  = 0;
                int height = 0;
                ::glfwGetWindowSize(control, &width, &height);
                return static_cast<std::uint64_t>(static_cast<unsigned>(width))
                     + static_cast<std::uint64_t>(static_cast<unsigned>(height));
            };
            const Measurement reference = measure(op_control);
            g_ladder_glfw_ns  = reference.m_median;
            g_ladder_has_glfw = true;

            // 三级阶梯的第三级：直接 Win32（OS/driver 侧）。只作成本分离陈述，不参与评级。
#if defined(_WIN32)
            const W::NativeWindowInfo native = window.get_native_window_info();
            if (native.backend == W::NativeBackend::Win32 && native.win32.hwnd != nullptr)
            {
                const HWND hwnd = static_cast<HWND>(native.win32.hwnd);
                auto op_os = [hwnd]() noexcept -> std::uint64_t {
                    RECT rect{};
                    ::GetClientRect(hwnd, &rect);
                    return static_cast<std::uint64_t>(rect.right) + static_cast<std::uint64_t>(rect.bottom);
                };
                g_ladder_os_ns  = measure(op_os).m_median;
                g_ladder_has_os = true;
            }
#endif

            std::string note = "reference=直接 glfwGetWindowSize";
            note += rounds_tail(reference);
            note += "；";
            note += kBaselineReason;
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.forward.get_size",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = reference.m_median,
                           .basis        = kBaselineBasis,
                           .note         = note });
#else
            std::string note = "reference 不可得";
            note += kNoControlNote;
            note += rounds_tail(plugin);
            RecordMetric({ .name = "window.forward.get_size", .actual_ns = plugin.m_median,
                           .reference_ns = 0.0, .basis = {}, .note = note });
#endif
        }

        // ================= 2) get_framebuffer_size =================

        {
            auto op_plugin = [&window]() noexcept -> std::uint64_t {
                const W::Window::FramebufferSize size = window.get_framebuffer_size();
                return static_cast<std::uint64_t>(size.width) + static_cast<std::uint64_t>(size.height);
            };
            const Measurement plugin = measure(op_plugin);

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
            GLFWwindow* const control = setup.m_control;
            auto op_control = [control]() noexcept -> std::uint64_t {
                int width  = 0;
                int height = 0;
                ::glfwGetFramebufferSize(control, &width, &height);
                return static_cast<std::uint64_t>(static_cast<unsigned>(width))
                     + static_cast<std::uint64_t>(static_cast<unsigned>(height));
            };
            const Measurement reference = measure(op_control);

            std::string note = "reference=直接 glfwGetFramebufferSize";
            note += rounds_tail(reference);
            note += "；";
            note += kBaselineReason;
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.forward.framebuffer_size",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = reference.m_median,
                           .basis        = kBaselineBasis,
                           .note         = note });
#else
            std::string note = "reference 不可得";
            note += kNoControlNote;
            note += rounds_tail(plugin);
            RecordMetric({ .name = "window.forward.framebuffer_size", .actual_ns = plugin.m_median,
                           .reference_ns = 0.0, .basis = {}, .note = note });
#endif
        }

        // ================= 3) get_status：七个状态位 =================

        {
            auto op_plugin = [&window]() noexcept -> std::uint64_t {
                const W::WindowStatus status = window.get_status();
                // 消费整个位集合：逐位累加，避免只用一个位就让编译器省掉其余查询
                std::uint64_t bits = 0;
                if (status.is_in_status(W::WindowStatusEnum::Visible))
                {
                    ++bits;
                }
                if (status.is_in_status(W::WindowStatusEnum::Focused))
                {
                    ++bits;
                }
                if (status.is_in_status(W::WindowStatusEnum::Iconified))
                {
                    ++bits;
                }
                if (status.is_in_status(W::WindowStatusEnum::Maximized))
                {
                    ++bits;
                }
                if (status.is_in_status(W::WindowStatusEnum::Resizable))
                {
                    ++bits;
                }
                if (status.is_in_status(W::WindowStatusEnum::Decorated))
                {
                    ++bits;
                }
                if (status.is_in_status(W::WindowStatusEnum::ShouldClose))
                {
                    ++bits;
                }
                return bits;
            };
            const Measurement plugin = measure(op_plugin);

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
            GLFWwindow* const control = setup.m_control;
            auto op_control = [control]() noexcept -> std::uint64_t {
                std::uint64_t bits = 0;
                bits += (::glfwGetWindowAttrib(control, GLFW_VISIBLE) == GLFW_TRUE) ? 1u : 0u;
                bits += (::glfwGetWindowAttrib(control, GLFW_FOCUSED) == GLFW_TRUE) ? 1u : 0u;
                bits += (::glfwGetWindowAttrib(control, GLFW_ICONIFIED) == GLFW_TRUE) ? 1u : 0u;
                bits += (::glfwGetWindowAttrib(control, GLFW_MAXIMIZED) == GLFW_TRUE) ? 1u : 0u;
                bits += (::glfwGetWindowAttrib(control, GLFW_RESIZABLE) == GLFW_TRUE) ? 1u : 0u;
                bits += (::glfwGetWindowAttrib(control, GLFW_DECORATED) == GLFW_TRUE) ? 1u : 0u;
                bits += (::glfwWindowShouldClose(control) == GLFW_TRUE) ? 1u : 0u;
                return bits;
            };
            const Measurement reference = measure(op_control);

            std::string note = "reference=直接接线（7 次 glfwGetWindowAttrib + 1 次 glfwWindowShouldClose，"
                               "与公开头列出的 7 个状态位一一对应）";
            note += rounds_tail(reference);
            note += "；";
            note += kBaselineReason;
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.forward.get_status",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = reference.m_median,
                           .basis        = kBaselineBasis,
                           .note         = note });
#else
            std::string note = "reference 不可得";
            note += kNoControlNote;
            note += rounds_tail(plugin);
            RecordMetric({ .name = "window.forward.get_status", .actual_ns = plugin.m_median,
                           .reference_ns = 0.0, .basis = {}, .note = note });
#endif
        }

        // ================= 4) get_key_state =================

        {
            auto op_plugin = [&window]() noexcept -> std::uint64_t {
                return window.get_key_state(W::KeyCodes::A) ? 1u : 0u;
            };
            const Measurement plugin = measure(op_plugin);

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
            GLFWwindow* const control = setup.m_control;
            auto op_control = [control]() noexcept -> std::uint64_t {
                return (::glfwGetKey(control, GLFW_KEY_A) == GLFW_PRESS) ? 1u : 0u;
            };
            const Measurement reference = measure(op_control);

            std::string note = "reference=直接 glfwGetKey(GLFW_KEY_A)";
            note += rounds_tail(reference);
            note += "；";
            note += kBaselineReason;
            note += "。本入口在平台调用之外还要做一次键域校验（Contract 层要求拒绝域外键码），"
                    "该部分是插件自身成本、不是接入成本，差值已包含在比值里";
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.forward.get_key_state",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = reference.m_median,
                           .basis        = kBaselineBasis,
                           .note         = note });
#else
            std::string note = "reference 不可得";
            note += kNoControlNote;
            note += rounds_tail(plugin);
            RecordMetric({ .name = "window.forward.get_key_state", .actual_ns = plugin.m_median,
                           .reference_ns = 0.0, .basis = {}, .note = note });
#endif
        }

        // ================= 5) is_visible =================

        {
            auto op_plugin = [&window]() noexcept -> std::uint64_t {
                return window.is_visible() ? 1u : 0u;
            };
            const Measurement plugin = measure(op_plugin);

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
            GLFWwindow* const control = setup.m_control;
            auto op_control = [control]() noexcept -> std::uint64_t {
                return (::glfwGetWindowAttrib(control, GLFW_VISIBLE) == GLFW_TRUE) ? 1u : 0u;
            };
            const Measurement reference = measure(op_control);

            std::string note = "reference=直接 glfwGetWindowAttrib(GLFW_VISIBLE)";
            note += rounds_tail(reference);
            note += "；";
            note += kBaselineReason;
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.forward.is_visible",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = reference.m_median,
                           .basis        = kBaselineBasis,
                           .note         = note });
#else
            std::string note = "reference 不可得";
            note += kNoControlNote;
            note += rounds_tail(plugin);
            RecordMetric({ .name = "window.forward.is_visible", .actual_ns = plugin.m_median,
                           .reference_ns = 0.0, .basis = {}, .note = note });
#endif
        }

        // ================= 6) 事件泵：poll_events =================

        {
            auto op_plugin = []() noexcept -> std::uint64_t {
                W::poll_events();
                return 1;
            };
            const Measurement plugin = measure(op_plugin);

#if IDWINDOW_BENCH_HAS_GLFW_CONTROL
            auto op_control = []() noexcept -> std::uint64_t {
                ::glfwPollEvents();
                return 1;
            };
            const Measurement reference = measure(op_control);

            std::string note = "reference=直接 glfwPollEvents（同一进程、同一 GLFW 状态、无待处理事件）";
            note += rounds_tail(reference);
            note += "；";
            note += kBaselineReason;
            note += "。注意：本项每次操作是一次完整事件泵（空泵），不是「每条消息」操作，"
                    "因此不借用任何场景预算（grading.md §2 E 的适用范围），只用同轮直接调用对照";
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.pump.poll_events",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = reference.m_median,
                           .basis        = kBaselineBasis,
                           .note         = note });
#else
            std::string note = "reference 不可得";
            note += kNoControlNote;
            note += rounds_tail(plugin);
            RecordMetric({ .name = "window.pump.poll_events", .actual_ns = plugin.m_median,
                           .reference_ns = 0.0, .basis = {}, .note = note });
#endif
        }

        // ================= 7) get_handle：纯取值入口 =================

        {
            auto op_plugin = [&window]() noexcept -> std::uint64_t {
                return static_cast<std::uint64_t>(window.get_handle().get_id());
            };
            const Measurement plugin = measure(op_plugin);

            auto op_control = []() noexcept -> std::uint64_t {
                return static_cast<std::uint64_t>(control_return_id());
            };
            const Measurement reference = measure(op_control);

            // 不给比值评级：grading.md §2 C 把"直接全局变量/数组访问"这类对照限定为**仅作数量级参考**，
            // 不作为分级依据；本项的绝对差只有一个周期量级，比值由调用骨架与计时粒度支配。
            std::string note = "同形状对照=一次非内联调用 + 一次成员读 + 返回值（不是平台调用）";
            note += rounds_tail(reference);
            note += "；本项 basis 留空 → ungraded：对照属 grading.md §2 C 限定为「仅作数量级参考」的直接取值对照，"
                    "不作为分级依据（实测绝对差只有约一个周期，比值受调用骨架与计时粒度支配）";
            note += rounds_tail(plugin);

            RecordMetric({ .name         = "window.handle.get_handle",
                           .actual_ns    = plugin.m_median,
                           .reference_ns = 0.0,
                           .basis        = {},
                           .note         = note });
        }

        // ================= 8/9) 事件派发路径：有/无回调两次对照 =================

        {
            // 有回调：set_size → 平台尺寸消息 → 插件派发 → 该窗口回调
            window.set_window_size_callback(&on_window_size, nullptr);
            std::uint32_t size_alternate = 0;
            auto op_with_callbacks = [&window, &size_alternate]() noexcept -> std::uint64_t {
                size_alternate = (size_alternate == 0) ? 1u : 0u;
                const std::uint32_t width = (size_alternate == 0) ? 320u : 336u;
                window.set_size(W::WindowSize{ width, 240 });
                return 1;
            };
            const Measurement with_callbacks = measure(op_with_callbacks);

            window.set_window_size_callback(nullptr, nullptr);
            auto op_without_callbacks = [&window, &size_alternate]() noexcept -> std::uint64_t {
                size_alternate = (size_alternate == 0) ? 1u : 0u;
                const std::uint32_t width = (size_alternate == 0) ? 320u : 336u;
                window.set_size(W::WindowSize{ width, 240 });
                return 1;
            };
            const Measurement without_callbacks = measure(op_without_callbacks);
            window.set_window_size_callback(nullptr, nullptr);

            std::string note = "一次 set_size 的往返（平台窗口调整 + 尺寸消息 + 插件派发）；"
                               "该指标不给比值评级：一次平台窗口调整是微秒级，插件派发（位集合判定 + 槽位查表 + "
                               "函数指针调用）是十纳秒级，两者同轮差值落在噪声内，无法分离出插件侧派发成本"
                               "（见 [UNCOVERED] window.cost.dispatch_separation）";
            note += rounds_tail(with_callbacks);
            note += "；无回调对照";
            note += rounds_tail(without_callbacks);
            note += "；本项只作数量级记录，回调累计计数=";
            note += std::to_string(g_callback_hits);

            RecordMetric({ .name         = "window.event.dispatch_roundtrip",
                           .actual_ns    = with_callbacks.m_median,
                           .reference_ns = 0.0,
                           .basis        = {},
                           .note         = note });

            Check(g_callback_hits > 0, "window.perf.dispatch_reached_callback",
                  std::string("派发确实到达该窗口的回调（计时窗口外的计数=")
                      + std::to_string(g_callback_hits) + "），否则上面的往返数字只测到了平台侧");
        }

        // ================= 零分配探针 =================

#if defined(ID_WINDOW_STATIC)
        {
            g_allocation_count.store(0, std::memory_order_relaxed);
            g_allocation_probe_active.store(true, std::memory_order_relaxed);

            std::uint64_t checksum = 0;
            for (std::size_t index = 0; index < kAllocationProbeIters; ++index)
            {
                const W::WindowSize size = window.get_size();
                const W::WindowStatus status = window.get_status();
                const std::uint16_t handle_id = window.get_handle().get_id();
                const bool visible = window.is_visible();
                const bool pressed = window.get_key_state(W::KeyCodes::A);

                checksum += static_cast<std::uint64_t>(size.width);
                checksum += status.is_none() ? 0u : 1u;
                checksum += static_cast<std::uint64_t>(handle_id);
                checksum += (visible ? 1u : 0u) + (pressed ? 1u : 0u);
            }

            g_allocation_probe_active.store(false, std::memory_order_relaxed);
            const std::uint64_t allocations = g_allocation_count.load(std::memory_order_relaxed);
            g_checksum ^= checksum;
            KeepAlive(&g_checksum);

            Check(allocations == 0, "window.perf.zero_allocation_forwarding",
                  std::string("转发路径（get_size/get_status/get_handle/is_visible/get_key_state）在 ")
                      + std::to_string(kAllocationProbeIters) + " 次调用中不得有堆分配；实测分配次数="
                      + std::to_string(allocations)
                      + "（探针 = 可执行文件内的全局 operator new 计数，覆盖静态链入的 IDWindow 与 vendor GLFW）");
        }
#else
        Uncovered("window.perf.zero_allocation_forwarding",
                  "SHARED 形态下 IDWindow 的分配发生在 IDWindow.dll 内，不经可执行文件的 operator new 替换"
                  "（不同 CRT 实例）；探针只能覆盖 exe 侧，因此本变体不取证。STATIC 变体有零分配证据");
#endif

        // ================= 未列入指标与无法分离的项 =================

        Uncovered("window.cost.lock_free_forwarding",
                  "转发路径是否加锁无法用计数探针观测：锁获取/释放不留可读计数，且契约要求窗口查询只在平台"
                  "主线程调用（不能构造竞争来暴露临界区）。间接证据（本报告 get_size 的三级阶梯里，插件转发"
                  "成本与一次平台调用的量级相同，未出现临界区级固定开销）只能作为推断，不作为「无锁」的证明");
        Uncovered("window.cost.dispatch_separation",
                  "事件派发的插件侧成本无法与 OS 窗口消息成本分离：一次 set_size 触发的平台窗口调整是微秒级，"
                  "插件派发是十纳秒级，同轮有/无回调两次测量的差值落在噪声内（两项读数见指标表），"
                  "因此只登记数量级，不给派发成本的比值");
        Uncovered("window.cost.init_and_cleanup",
                  "init/shutdown 与窗口创建/销毁属「每进程一次」成本，本轮未列入指标（未列入不等于 0）："
                  "本域按 dispatch/forwarding 的逐次路径取证，初始化与清理成本需要另立带场景假设的独立口径");

        release_windows(setup);
    }

    void IdWindowPerformanceNotes() noexcept
    {
        ReportLine("    - 统计口径：每项指标 = 预热（半轮，不计时）+ 5 轮时间盒计时，每轮 ≥130 ms，"
                   "报 5 轮中位数；min/max/spread 与单轮迭代数写在该指标 note 里。"
                   "时间盒每 64 次迭代读一次计时源，摊到每次操作 <1 ns（计时源读数约 20–30 ns）。");
        ReportLine("    - 极差判定：spread=(max-min)/median > 20% 时由 Summarize 标 unstable，"
                   "用例侧把 basis 留空 → 该项 ungraded 并计入退出码（grading.md §5/§6），不做有效评级。");
        ReportLine("    - 参考值来源：全部为 os-baseline（同轮、同进程、相邻位置的直接 GLFW 等价接线），"
                   "目标倍数 1.0，理由：被测入口是薄封装，转发路径不应引入超过一倍平台调用成本；"
                   "没有使用任何 budget/theoretical 值，也没有跨机器、跨配置借用读数。");
        ReportLine("    - 插件自身成本与接入成本的分工：插件自身成本 = 「插件入口 − 直接 GLFW 调用」的差值，"
                   "接入成本 = 与插件自身无关的调用边界（STATIC 形态下公开入口可被内联、SHARED 形态下是"
                   "跨 DLL 导入调用）。两形态的读数只并列展示，不互相借用参考值（grading.md §6）。");
        ReportLine("    - OS/driver 侧成本：`get_size` 额外给出三级阶梯（Win32 GetClientRect → 直接 GLFW → "
                   "插件转发），阶梯差值只写在 measurement notes，不参与评级（见下方 ladder 行）。");
        {
            std::string ladder = "    - ladder (get_size)     : ";
            if (g_ladder_has_os)
            {
                ladder += "win32 GetClientRect=";
                ladder += FormatDouble(g_ladder_os_ns, 2);
                ladder += " ns → ";
            }
            else
            {
                ladder += "win32 GetClientRect=未测 → ";
            }
            ladder += g_ladder_has_glfw ? ("glfwGetWindowSize=" + FormatDouble(g_ladder_glfw_ns, 2) + " ns → ")
                                        : std::string("glfwGetWindowSize=不可得 → ");
            ladder += "plugin get_size=";
            ladder += FormatDouble(g_ladder_plugin_ns, 2);
            ladder += " ns；插件转发成本 = plugin − glfw，GLFW 层成本 = glfw − win32，win32 一项即 OS/driver 侧成本；"
                      "三级各自 5 轮中位数，阶梯差值不参与评级";
            ReportLine(ladder);
        }
        ReportLine("    - 分配与锁：零分配由可执行文件的全局 operator new 计数探针取证（STATIC 变体），"
                   "锁的存在性无法用探针观测 → 记 [UNCOVERED]，并给出「未出现临界区级固定开销」的间接推断。");
        ReportLine("    - 未列入指标的成本：init/shutdown、窗口创建/销毁、GL 上下文创建与共享、"
                   "事件派发分离 → 见 [UNCOVERED] 行，未列入不等于 0。");
        ReportLine("    - 未验证：MSVC/clang 与 32 位目标的读数、Linux/macOS、SHARED 形态的直接对照"
                   "（不可得，见指标 note）、跨进程 5 连跑资格（framework.md §4 未进入代码自动判定，"
                   "本轮按变体归档 repeat-runs.txt 供人工核对）。");
    }
} // namespace ID::Bench
