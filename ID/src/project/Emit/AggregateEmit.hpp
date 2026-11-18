#pragma once

#include <vector>

#include "cmake/Common/Diagnostic.hpp"
#include "project/Emit/GeneratedTexts.hpp"
#include "project/Resolve/Resolution.hpp"

/**
 *  `IDPlugin/CMakeLists.txt` 的模型装配与整文件产出（docs/id/08 §8.6、spec §11.3）。
 *
 *  内容顺序：`cmake_minimum_required` → `project(IDPlugin)` → 汇总目标（`INTERFACE` 库，第 6 项）→
 *  它的 `target_include_directories`（第 10 项）→ 共享 vendor 的集中接入与插件 `add_subdirectory`
 *  （第 13 项）→ 第 13 项尾部的 vendor 运行期输出属性适配块。
 *
 *  关键不变量：
 *  1. 汇总目标取 `NamingPlan.common_target`，不写死字面量；它是 `INTERFACE` 库，不设输出属性与编译特性。
 *  2. 共享 vendor 只接入 `Resolution.vendor_dirs` 命中的目录、按 `dir` 去重、只接入一次，且排在插件
 *     `add_subdirectory` 之前；接入经 `create_referenced_subdirectory` 装配，不手拼命令行文本。
 *  3. 每个接入点前写 `if(TARGET <target>)` + `message(FATAL_ERROR ...)` 的拒绝守卫，不是静默跳过。
 *  4. 插件 `add_subdirectory` 的顺序**等于** `Resolution.init_order`（被依赖者在前）。
 *  5. vendor 适配块写在全部 `add_subdirectory` 之后，只写 `RUNTIME_` / `LIBRARY_` 的 `_DEBUG` / `_RELEASE`
 *     四项属性，取值只拼 `${ID_OUTPUT_ROOT}/debug` 与 `${ID_OUTPUT_ROOT}/release`；`Vendor/**` 不被改写。
 *  6. 无法自证适配时（`dir` / `target` 为空、`dir` 不在 `vendor_dirs`、`target` 不在 `vendor_targets`）
 *     不写该条接入与适配块，产出一条 `warn` 级日志并在 `limitations` 里登记
 *     `vendor_runtime_output_not_adapted`（说明串写明 `dir` 与 `target`），不改退出码、不判为失败。
 *
 *  首版支持域（spec §11.6 末条）：只保证"不产生运行期文件的 vendor"（`target: null` 只复制，或纯头 vendor）
 *  与"属性适配后产物落 `bin` 的编译型 vendor"；无法适配或适配失效的情形必须出现在 `limitations` 里，
 *  不得读成"已完成同目录保证"。
 */

namespace ID::Project
{
    /**
     *  聚合文件的装配结果：整文件文本、降级限制与校验报告。
     */
    struct AggregateEmitResult
    {
        GeneratedText               aggregate   = { };   // IDPlugin/CMakeLists.txt；校验失败时为空
        std::vector<EmitLimitation> limitations = { };   // 本次装配的降级项（vendor 适配缺口）
        CMake::ValidationReport     report      = { };   // 聚合模型的校验报告
    };

    /**
     *  @brief 装配聚合目录模型并产出 `IDPlugin/CMakeLists.txt`
     *  @param assembly 组装结果（只读；取汇总目标名、vendor 目录与目标、拓扑序）
     *  @return 整文件文本、降级限制与校验报告
     *  @note 不写盘、不读磁盘；相同输入产出逐字节相同的文本
     *  @note `limitations` 在校验失败时同样产出：它是行为限制通道，不是错误通道
     */
    AggregateEmitResult emit_aggregate(const ResolvedAssembly& assembly);
} // namespace ID::Project
