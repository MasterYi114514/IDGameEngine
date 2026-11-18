#pragma once

#include <string>
#include <vector>

#include "cmake/Common/Diagnostic.hpp"

/**
 *  仓库自描述：按单处插件数据表构造 `IDPlugin` 下四份 `CMakeLists.txt` 的模型，产出整文件文本。
 *
 *  职责边界：只构造模型与产出文本。不解析 `plugins.json` / `plugin_config.json`、不按目录扫描发现
 *  数据表之外的插件、不落盘、不做诊断分类（CMake 封装计划书 §5.9）。落盘（`write_whole_file`）、
 *  `--check` 比较与退出码由 CLI 负责；落盘前的结构校验由 `validate` 负责。
 *
 *  过渡例外：手写数据表只允许存在于 `src/generate/**`（计划书 §5.9、`skills/id-kernel/references/plugins.md`
 *  第 6 节）。通用生成仍须由 resolved 记录驱动；插件名不得扩散到 `src/cmake/**` 与 emitter，emitter 里
 *  不出现任何插件名分支——数据表一旦被清单解析取代，本模块即退化为该解析结果的装配层。
 *
 *  关键不变量：顺序固定为「构造模型 → validate → emit_directory」。`emit_target` / `emit_directory`
 *  对非法模型有前置断言（`TargetType::None`、缺收源目录），因此校验不通过时不产出任何文本，
 *  调用方也就拿不到可写出的内容。
 *
 *  产物形态：四个目标文件全部是 `EmitScope::WholeFile` 且不传 `GeneratedHeader`——仓库内建模文件按
 *  `cmake-style` 第 2 节首行即 `cmake_minimum_required(VERSION 4.1.0)`。生成文本与既有手写文件
 *  只保语义等价，不承诺逐字节一致（中文注释、空行分组与 `${...}` 的引号形态是已知差异）。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，`std::string` / `std::vector` 的分配成本
 *  可接受，因此不做逐点分配失败检查——这是对 `cpp-style` 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::Generate
{
    /**
     *  一份待落盘文件：相对 `--root` 的 POSIX 分隔路径与模型的整文件文本。
     */
    struct SelfFile
    {
        std::string relative_path = {};   // 相对工程根的 POSIX 分隔路径，如 "IDPlugin/IDJson/CMakeLists.txt"
        std::string content       = {};   // emit_directory 产出的整文件文本，以 '\n' 结尾
    };

    /**
     *  一次自描述的结果：校验汇总与待落盘清单。`report.is_valid` 为假时 `files` 为空——
     *  emit_* 只在校验通过后执行，调用方不会拿到未校验的文本。
     */
    struct SelfBundle
    {
        CMake::ValidationReport report = {};   // 四份模型的校验汇总，按数据表顺序追加
        std::vector<SelfFile>   files  = {};   // 待落盘清单；与 report.is_valid 同时有效
    };

    /**
     *  @brief 构造四份模型、先校验再产出文本
     *  @return 校验汇总与待落盘清单；校验失败时清单为空
     *  @note 四份模型的构造顺序、校验顺序与清单顺序都等于数据表顺序，产物因此是确定性的：
     *        相同输入产生相同文本与相同路径，不嵌入时间戳、随机顺序或本机路径
     */
    SelfBundle make_self_bundle();
} // namespace ID::Generate
