#include <cstddef>
#include <cstdio>
#include <cstring>

/**
 *  工具链探针（fixture，不属于官方测试目标，不写官方报告）。
 *  
 *  目的：为 `ID::Bench::FormatDouble` 的规避方案提供可复现证据。
 *  背景：本机 GCC 16.2.0 + UCRT 下，一次传入 11 个可变参数的 `snprintf` 在运行期触发访问冲突
 *  （异常码 0xC0000005），因此报告拼接被改成"每次只格式化一个 double + 字符串连接"。
 *  
 *  用法：
 *    format_probe.exe many     # 11 个可变参数：预期崩溃（非 0 退出码，Windows 为 0xC0000005）
 *    format_probe.exe single   # 单值格式化：预期正常完成（退出码 0）
 *  
 *  判定：两种模式的退出码与该模式的结论一起写进调用方日志，不在这里做断言。
 */

namespace
{
    char g_many_buffer[512] = {};
    char g_single_buffer[64] = {};

    // 模式 many：10 个转换说明、11 个可变参数（与整改前的 note 拼接形态一致）
    void RunManyArguments() noexcept
    {
        const char* format =
            "%.0f 条/秒；中位/最小/最大=%.2f/%.2f/%.2f ns；迭代=%zu/轮×%d 轮（单轮≈%.1f ms）；极差=%.1f%%%s%s";

        std::snprintf(g_many_buffer, sizeof(g_many_buffer), format,
                      810000.0, 1234.5, 1234.5, 1200.0, 1300.0,
                      static_cast<std::size_t>(50000), 5, 61.7, 8.0,
                      false ? "；unstable" : "", "；sink=stdout_color_sink_mt");
        std::fputs(g_many_buffer, stdout);
        std::fputc('\n', stdout);
    }

    // 模式 single：FormatDouble 的现行写法（每次只传一个 double）
    void RunSingleValue() noexcept
    {
        std::snprintf(g_single_buffer, sizeof(g_single_buffer), "%.*f", 2, 1234.5);
        std::fputs(g_single_buffer, stdout);
        std::fputc('\n', stdout);
    }
} // namespace

int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "single";

    if (std::strcmp(mode, "many") == 0)
    {
        RunManyArguments();
        return 0;
    }

    if (std::strcmp(mode, "single") == 0)
    {
        RunSingleValue();
        return 0;
    }

    std::fputs("usage: format_probe [many|single]\n", stderr);
    return 2;
}
