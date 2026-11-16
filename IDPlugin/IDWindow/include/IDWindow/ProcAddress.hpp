#pragma once

/**
 *  图形 API 入口函数地址：通用函数指针，不是 `void*`。
 *
 *  不变量：
 *  1. 只在 `ID::Window` 定义这一个名字，不另立 `GLProcAddress` 等同义别名。
 *  2. 它是 GL loader 领域的通行类型名：消费者用自己的 loader 把它转换成具体签名后调用；
 *     本插件不提供 typed 转换，也不假设不同上下文返回同一地址。
 */

namespace ID::Window
{
    using ProcAddress = void(*)();
} // namespace ID::Window
