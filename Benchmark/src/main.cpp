#include <cstring>

#include "benchmark.hpp"
#include "window/window_domain.hpp"

/**
 *  官方测试可执行文件入口。
 *  
 *  - 正常模式：一次会话内跑完全部已登记域，输出走公共设施，退出码见 benchmark_support.hpp。
 *  - 子进程模式：由父进程按参数分裂自身，只跑"契约会终止进程"或"违约用法可能崩溃"的那个用例，
 *    父进程按退出码集合与实际落盘内容判定（见 methodology 第 4 节）。
 *    - `--fatal-child=<log path>`：fatal 终止语义（契约要求 std::abort()）。
 *    - `--stale-child=<log path>`：陈旧 `Logger*` 违约用法（实现行为：不崩溃、消息被丢弃；契约不承诺安全）。
 *    - `--destroyed-config-child`：已销毁日志器上调用四个配置方法（实现行为：返回 false 且不崩溃；
 *      违约用法，契约不承诺安全）。
 */

namespace
{
    constexpr const char* kFatalChildPrefix           = "--fatal-child=";
    constexpr const char* kStaleChildPrefix           = "--stale-child=";
    constexpr const char* kDestroyedConfigChildFlag   = "--destroyed-config-child";
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strncmp(argv[1], kFatalChildPrefix, std::strlen(kFatalChildPrefix)) == 0)
    {
        return ID::Bench::RunFatalChildCase(argv[1] + std::strlen(kFatalChildPrefix));
    }

    if (argc >= 2 && std::strncmp(argv[1], kStaleChildPrefix, std::strlen(kStaleChildPrefix)) == 0)
    {
        return ID::Bench::RunStalePointerChildCase(argv[1] + std::strlen(kStaleChildPrefix));
    }

    if (argc >= 2 && std::strcmp(argv[1], kDestroyedConfigChildFlag) == 0)
    {
        return ID::Bench::RunDestroyedConfigChildCase();
    }

    ID::Bench::BeginSession();
    ID::Bench::Run("IDLog", ID::Bench::IdLogCorrectnessTest, ID::Bench::IdLogPerformanceTest,
                   ID::Bench::IdLogPerformanceNotes);
    // IDJson 契约没有"终止进程"的路径，因此本域不需要子进程分裂，主入口不为它增加子进程参数分支
    ID::Bench::Run("IDJson", ID::Bench::IdJsonCorrectnessTest, ID::Bench::IdJsonPerformanceTest,
                   ID::Bench::IdJsonPerformanceNotes);
    // IDWindow 契约里会终止进程的入口（get_window / get_current_window 的违约用法）本轮无子进程隔离，
    // 因此本域不为它增加子进程参数分支（按 Uncovered 登记）
    ID::Bench::Run("IDWindow", ID::Bench::IdWindowCorrectnessTest, ID::Bench::IdWindowPerformanceTest,
                   ID::Bench::IdWindowPerformanceNotes);
    // ID 是工具全域（子域 id.cmake 描述模型 + id.cli 命令行 + 预留 id.json/id.resolve/id.generate）：
    // 一次注册覆盖全域，子域靠用例名前缀区分。本域不新增子进程参数分支——被测契约里会终止进程的两个入口
    // （Writer::end_scope 缩进下溢、BodyOnly 与非空 header 同时出现）按 Uncovered 登记障碍；
    // id.cli 用例自己以子进程方式调用被测工具（BENCHMARK_ID_EXE），不复用本进程的分裂参数
    ID::Bench::Run("ID", ID::Bench::IdCorrectnessTest, ID::Bench::IdPerformanceTest,
                   ID::Bench::IdPerformanceNotes);
    ID::Bench::EndSession();
    return ID::Bench::ExitCode();
}
