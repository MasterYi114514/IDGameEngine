#pragma once

#include <cstddef>

/**
 *  工具侧内部断言：内部不变量被破坏或调用方违约时终止进程。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。断言不承担可恢复失败：
 *  I/O 与写入失败一律走 Common/Diagnostic.hpp 的诊断通道。
 *
 *  过渡状态：IDPlugin/Common 目前没有统一的断言入口，本头是工具内的过渡定义。IDPlugin/Common 的
 *  Core 提供统一 ID_ASSERT 后，本头应改为引用那一份，并在同一批改动里替换本头与全部调用点，
 *  不保留两份并存的断言实现。
 */

namespace ID::CMake
{
    /**
     *  @brief 断言失败的唯一终止出口
     *  @param expression 未求值为真的表达式原文，由 ID_ASSERT 用 #expr 传入，不得为 nullptr
     *  @param file 触发断言的文件路径，不得为 nullptr
     *  @param line 触发断言的行号
     *  @note 不返回：向 stderr 写出表达式、文件与行号后 std::abort()，不分配内存、不做格式化拼接，
     *        以便在任何状态下都能完成输出。
     *  @note 只用于调用方违约与内部不变量破坏（cpp-style 第 7、9 节）；可恢复失败不得经过本函数，
     *        输入错误、文件不存在等可诊断问题走诊断通道。
     */
    [[noreturn]] void assertion_failed(const char* expression, const char* file, std::size_t line) noexcept;
} // namespace ID::CMake

#define ID_ASSERT(expr)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if (!(expr)) [[unlikely]]                                                                  \
        {                                                                                          \
            ::ID::CMake::assertion_failed(#expr, __FILE__, __LINE__);                              \
        }                                                                                          \
    } while (false)
