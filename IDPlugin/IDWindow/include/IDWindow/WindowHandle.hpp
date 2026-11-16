#pragma once

#include <cstdint>
#include <type_traits>

#include "IDHandle.hpp"

/**
 *  窗口句柄：`WindowUINT` 与 `WindowHandle` 只在本头定义一次，`Window.hpp` 与 `WindowPool.hpp`
 *  都经本头取得这两个名字，不各自重复声明。
 *
 *  不变量：
 *  1. 句柄宽度是 `std::uint16_t`，没有加宽、没有代数计数器；陈旧句柄因此不可检测。
 *  2. 有效句柄只能由 `WindowPool`（`Common::Handle` 的 `Factory` 参数）造出；
 *     外部默认构造只能得到无效句柄。
 *  3. `WindowPool` 在本头只前置声明 —— 句柄别名需要它作为 `Factory`，不需要完整类型。
 */

namespace ID::Window
{
    class WindowPool;

    using WindowUINT   = std::uint16_t;                              // 与句柄宽度一致，不加宽
    using WindowHandle = Common::Handle<WindowUINT, WindowPool>;     // Factory = WindowPool

    static_assert(sizeof(WindowUINT) == sizeof(std::uint16_t), "WindowUINT 必须是 2 字节，否则句柄宽度契约与既有消费者失效");
    static_assert(std::is_trivially_copyable_v<WindowHandle>, "WindowHandle 必须可平凡复制，否则不能跨模块按值传递");
} // namespace ID::Window
