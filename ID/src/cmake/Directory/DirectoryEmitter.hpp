#pragma once

#include <string>

#include "cmake/Directory/Directory.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Value/Argument.hpp"

/**
 *  目录文本产出：把一个 Directory 渲染成整文件文本（骨架第 1–13 项）或只含目录体的文本（第 3–13 项）。
 *
 *  职责边界：只排版，不写文件、不校验模型、不决定落盘模式。骨架顺序、空行分组、缩进与两类头部规则
 *  都由本组函数单方决定；写入方式（整文件写入 / 受控片段替换）由调用方按 EmitScope 映射，emitter 不参与。
 *
 *  依赖方向：本头单向依赖 Project/Project.hpp（取 EmitScope）与 Value/Argument.hpp（取 Argument），
 *  Project 不引用任何 emitter 头。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  用户项目生成文件的头部注释取值：所有权标注与生成来源各占一行，位于第 1 项之前。
     *  取值按 Argument::get_text() 原样写出（注释不是参数，不经参数渲染）：只允许 Bare / Quoted /
     *  QuotedExpand，传 Verbatim 属调用方违约。
     */
    struct GeneratedHeader
    {
        Argument tool_name;       // 工具名，取值 "ID"
        Argument source_record;   // 本次 resolved 记录的相对路径
    };

    /**
     *  @brief 产出目录文本
     *  @param directory 目录
     *  @param scope 产出范围：整文件（第 1、2 项 + 目录体）、只含目录体（第 3–13 项），
     *               或按骨架分段（`HeaderOnly` = 第 1、2 项；`ConfigurationOnly` = 第 3、4 项；
     *               `TargetsOnly` = 第 5–13 项）。三段依次拼接、段与段之间一个空行，就是整文件的正文
     *  @param header 头部注释；仓库内建模文件传空指针，用户项目生成文件传非空指针
     *  @return 以 '\n' 结尾的文本；项与项之间恰有一个空行，同一项内不插空行；首尾不带空行
     *  @note 只有 `WholeFile` 可以配非空 header：`BodyOnly` 与三个分段 scope 都是受控片段，只能配空指针，
     *        否则属调用方违约，用 ID_ASSERT 终止；
     *        header 的取值文本含 CR/LF 或其它控制字符由组装阶段在构造前单独判定，不在本函数内
     */
    std::string emit_directory(const Directory& directory,
                               EmitScope scope = EmitScope::WholeFile,
                               const GeneratedHeader* header = nullptr);
} // namespace ID::CMake
