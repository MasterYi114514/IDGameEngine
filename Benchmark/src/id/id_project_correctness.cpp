#include "benchmark_id.hpp"
#include "benchmark_support.hpp"

#include "id_project_cases.hpp"
#include "id_project_test_support.hpp"

/**
 *  `id.project` 子域入口（`ID` 工具全域下的一个子域，用例名 `id.project.*`）。
 *
 *  被测对象：`ID project` 的六条子命令（`new` / `update` / `build` / `clean` / `recover` / `list`）与四类记录
 *  （`project.json` / `plugins.json` / `resolved.json` / `manifest.json`，以及 `intent.json` / `build.json`）。
 *  契约源：`docs/id-project-flow-spec.md` §15 与 `docs/id/11-verification-matrix.md`。
 *
 *  隔离：会话根 `<BENCHMARK_TEMP_DIR>/id.project/` 由本入口建一次、结束删除；每个用例在其中拥有独立
 *  `<case>/engine`（假引擎根，含 `ID/` 与 `IDPlugin/`）与 `<case>/project`（项目根），互不祖先，
 *  不触碰工程根的 `ID/project.json` 与真实 `IDPlugin/`。
 *
 *  边界：本子域只出正确性判定；工具耗时不是游戏性能，不进入性能区评级。
 */

namespace ID::Bench
{
    void IdProjectCorrectnessTest() noexcept
    {
        ID::Bench::IdProject::BeginIdProjectSession();

        ReportLine("config (id.project): 子域 = ID 域的 id.project（用例名 id.project.*）；被测对象 = `ID project` 的 "
                   "new/update/build/clean/recover/list 与 project.json/plugins.json/resolved.json/manifest.json/"
                   "intent.json/build.json");
        ReportLine("config (id.project): 隔离 = 会话根 <BENCHMARK_TEMP_DIR>/id.project/，每用例独立 <case>/engine"
                   "（假引擎根，含 ID/ 与 IDPlugin/）与 <case>/project；项目根不在工程根内，不写真实 IDPlugin/ 与工程根 "
                   "ID/project.json");
        ReportLine("config (id.project): 驱动 = 进程内 ID::Project::run_command_line（异常终止类走子进程 "
                   "BENCHMARK_ID_EXE + ID_TEST_INJECT，期望码 124）；真实构建 = --generator \"MinGW Makefiles\" "
                   "--toolchain mingw --arch x64，产物断言 <项目根>/bin/mingw/x64/<config>/");
        ReportLine("config (id.project): 摘要 = 测试侧独立 SHA-256（FIPS 已自检），不与被测实现的 Digest 共享代码");
        ReportLine("config (id.project): 未验证组合 = Linux 与非 MinGW 工具链（本机不可用）；同秒日志样本、"
                   "真实并发重叠、changed_since_plan 窗口按矩阵 §11.6 U-1…U-7 登记 Uncovered；"
                   "非 ASCII 项目路径（`id.project.i7.non_ascii_path`）因本机 mingw32-make 不支持该路径登记 Uncovered"
                   "（最小 CMake 工程已独立复现 configure 0 / build 2），纯 ASCII 含空格路径由 "
                   "`id.project.i7.ascii_space_path` 真实取证；"
                   "「返回 0 但产物缺失」的 artifact_missing 窗口（`id.project.i5`）因本机 mingw32-make 会重新链接"
                   "而不可构造，登记 Uncovered，产物判定逻辑由 i6 的 no-op 用例与 artifact 路径断言覆盖");

        if (ID::Bench::IdProject::SessionRoot().empty())
        {
            Uncovered("id.project.all", "构建未注入 BENCHMARK_TEMP_DIR，无法建立隔离夹具根；不猜路径");
            return;
        }
        if (!ID::Bench::IdProject::IsDigestSelfTestOk())
        {
            Uncovered("id.project.all", "测试侧独立 SHA-256 未通过 FIPS 已知向量自检，摘要类断言无依据；"
                                       "不换成被测实现的摘要");
            return;
        }

        IdProjectCasesAB();
        IdProjectCasesC();
        IdProjectCasesDE();
        IdProjectCasesF();
        IdProjectCasesH();
        IdProjectCasesI();
        IdProjectCasesJKLMN();

        ID::Bench::IdProject::EndIdProjectSession();
    }
} // namespace ID::Bench
