#pragma once

#include <string>

#include "cmake/Target/Target.hpp"

/**
 *  目标块文本产出：把一个 Target 渲染成 cmake-style 第 2 节第 5–12 项的连续文本。
 *
 *  职责边界：只排版，不写文件、不校验模型、不改变模型。骨架顺序、空行分组与缩进由本组函数单方决定，
 *  模型不存语句序号；合法性（类型、设置项、作用域、参数形态、链接目标名）由
 *  validate(const Project&) 在生成前判定，本组函数不重复校验，只对"调用方必须先校验"的前置条件断言。
 *
 *  imported 目标的块形态、形态选项两段形态与 run 合并规则都只有这一份实现，不另立第二套排版规则。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  @brief 产出目标守卫块文本：`if(TARGET <名>) message(FATAL_ERROR <说明>) endif()`
     *  @param guard 守卫的目标名与说明串；调用前必须已通过 validate
     *  @return 以 '\n' 结尾的文本，体内 4 空格缩进、`if` 与 `endif()` 顶格，首尾不带空行
     *  @note 目标块（第 5 与第 6 项之间）与子目录接入（第 13 项）共用本函数，只有这一份守卫排版实现；
     *        本函数不写文件、不决定前后空行（由调用方按所在项决定）
     */
    std::string make_target_guard(const TargetGuard& guard);

    /**
     *  @brief 产出单个目标的目标块文本（骨架第 5–12 项）
     *  @param target 目标；调用前必须已通过 validate，且类型不是哨兵 None
     *  @return 以 '\n' 结尾的文本：块内相邻两项之间恰有一个空行、块内不插其它空行，首尾不带空行
     *  @note 调用方负责目标块之间的空行（emit_directory 在块之间写一个空行）；本函数不写文件
     *  @note SYSTEM 是命令级选项（`target_include_directories(<target> [SYSTEM] [BEFORE|AFTER] <scope> ...)`）：
     *        include 条目里 is_system 发生变化时结束当前命令并另开一条同命令，SYSTEM 只写在该命令第一个
     *        作用域段之前；命令之间不插空行。非 include 的三类语句不输出 SYSTEM。
     */
    std::string emit_target(const Target& target);
} // namespace ID::CMake
