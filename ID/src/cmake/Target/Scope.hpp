#pragma once

#include <cstdint>
#include <string_view>

/**
 *  语句条目的作用域：target_compile_definitions / target_include_directories /
 *  target_link_directories / target_link_libraries 的 PUBLIC / PRIVATE / INTERFACE 关键字。
 *
 *  职责边界：只承载作用域取值与它的关键字文本。同一条命令内各作用域段的先后由条目插入顺序决定：
 *  emitter 不按作用域重排、不合并同名条目（include 的搜索优先级与链接库的解析顺序属于语义）。
 *  合法性（INTERFACE 库与 imported 目标只允许 Interface）由 validate 判定，枚举本身不做运行期校验。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  作用域取值。Public 显式取 0，便于按插入顺序与哨兵判断时无隐含偏移。
     */
    enum class Scope : std::uint8_t
    {
        Public    = 0,   // 对消费者可见并作用于自身
        Private   = 1,   // 只作用于本目标
        Interface = 2,   // 只对消费者可见
    };

    static_assert(sizeof(Scope) == sizeof(std::uint8_t),
                  "Scope 的底层类型必须与声明一致，否则按值传递与诊断取值约定失效");

    /**
     *  @brief 取得作用域在 CMake 命令里的大写关键字
     *  @param scope 作用域取值
     *  @return "PUBLIC" / "PRIVATE" / "INTERFACE"；指向静态存储，生命周期覆盖整个进程
     *  @note 纯映射，不做合法性判断；SYSTEM 前缀由 emitter 单独拼接（SYSTEM 写在作用域关键字之前）
     */
    constexpr std::string_view scope_keyword(Scope scope) noexcept
    {
        switch (scope)
        {
            case Scope::Public:    return "PUBLIC";
            case Scope::Private:   return "PRIVATE";
            case Scope::Interface: return "INTERFACE";
        }
        return "";
    }
} // namespace ID::CMake
