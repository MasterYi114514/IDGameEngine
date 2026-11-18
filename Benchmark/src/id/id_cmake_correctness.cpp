#include "benchmark_id.hpp"

#include "benchmark_support.hpp"
#include "id_cmake_test_support.hpp"

/**
 *  id.cmake 正确性区入口：按被测层顺序调用各层用例（值/文本 → 目标 → 目录 → 工程 → validate → 落盘）。
 *
 *  被测对象与取证口径写在区域开头的 `config (id.cmake)` 行里（报告与控制台走同一通道），
 *  每一层的判定依据写在各层用例文件的文件头注释里；本文件只做编排与口径声明，不含具体断言。
 *
 *  纪律：各层用例自包含（自建模型、自建临时项目根），因此这里的调用顺序不构成隐式状态依赖。
 */

namespace ID::Bench
{
    void IdCmakeCorrectnessTest() noexcept
    {
        ReportLine("config (id.cmake): 被测对象 = IDCore 的 src/cmake/**（Argument/Writer/Target/Directory/Project/"
                   "两个 emitter/validate/collect_outputs/FragmentWriter）；消费方式 = 普通静态库 + 公开 include 根，"
                   "不经 ID 的生成接线、无运行期服务槽");
        ReportLine("config (id.cmake): 契约源 = docs/cmake-implementation-plan.md §5.2 值层、§5.3 文本层、"
                   "§5.7 emitter 输出算法、§5.8 落盘与 24 类诊断、§5.11 IMPORTED 契约；"
                   "用例逐层对照该契约与公开头顶部注释，不按实现猜测");
        ReportLine("config (id.cmake): 取证方式 = 全文字节比对（整文件/目标块）+ 按空行切块的逐块比对 + "
                   "诊断按『类别 + subject』定位 + 落盘后按字节读回；文本比对与诊断定位只有一份实现"
                   "（src/id/id_cmake_test_support.cpp）");
        ReportLine("config (id.cmake): 未纳入本子域 = CLI（ID cmake self）的用法、退出码与生成/--check 幂等"
                   "由同域的 id.cli 子域覆盖；ID/src/generate/** 自描述模型只作只读参考");

        RunCmakeValueCases();
        RunCmakeTextCases();
        RunCmakeTargetCases();
        RunCmakeDirectoryCases();
        RunCmakeProjectCases();
        RunCmakeValidateCases();
        RunCmakeFileCases();
    }
} // namespace ID::Bench
