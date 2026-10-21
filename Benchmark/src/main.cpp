#include "benchmark.hpp"

/**
 *  Benchmark 入口
 *  1. 只允许 include 伞形头，不写任何用例逻辑；每个测试域一行 Run(...)；
 *  2. 域清单靠这里的显式调用维护，禁止静态自注册（项目红线：不许运行期全局构造）。
 */

int main()
{
    ID::Bench::BeginSession();

    ID::Bench::Run("ID.Kernel", &ID::Bench::IDKernelSecurityTest, &ID::Bench::IDKernelPerformanceTest);
    ID::Bench::Run("ID.Log",    &ID::Bench::IDLogSecurityTest,    &ID::Bench::IDLogPerformanceTest);

    ID::Bench::EndSession();
    return ID::Bench::ExitCode();
}
