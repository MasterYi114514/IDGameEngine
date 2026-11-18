#pragma once

#include <cstdint>

/**
 *  目标类型：一个 CMake 目标在 add_library / add_executable 上的形态集合。
 *
 *  职责边界：只回答"目标是什么形态"，不携带语句形态、作用域或输出目录信息——那些由
 *  TargetEmitter 按 cmake-style 第 2 节的骨架顺序产出、由 Scope 与 Target 字段表达。
 *
 *  None 是哨兵，放最后，不是可建模的类型：Directory::create_target 只登记名字与类型、不拒绝 None，
 *  生成前由 validate 报 InvalidTargetType（cpp-style 第 6 节：哨兵不是合法取值）。
 *  ObjectLibrary 不在首版取值集内，需要时另行确认后补入并同时补输出属性与语句块的展开规则。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  目标类型。前四项是可建模的类型，None 在最后作哨兵。
     */
    enum class TargetType : std::uint8_t
    {
        Executable       = 0,   // add_executable
        StaticLibrary    = 1,   // add_library(... STATIC ...)
        SharedLibrary    = 2,   // add_library(... SHARED ...)
        InterfaceLibrary = 3,   // add_library(... INTERFACE)：没有编译单元，不产生产物
        None             = 4,   // 哨兵：不是可建模类型，生成前由 validate 拒绝
    };

    static_assert(sizeof(TargetType) == sizeof(std::uint8_t),
                  "TargetType 的底层类型必须与声明一致，否则按值传递与诊断取值约定失效");
} // namespace ID::CMake
