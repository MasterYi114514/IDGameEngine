#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

/**
 *  原生窗口借用信息：按后端 tag 读取对应分支，全部是**借用**句柄/指针，不转移所有权。
 *
 *  形态：tagged union 载体（`skills/cpp-style/references/design-patterns.md` 第 5 节）——
 *  `backend` 在偏移 0、union 紧随其后，用一组 `static_assert` 钉住布局；默认构造同时给出确定的
 *  tag 与**已激活的分支**。本类型没有分字段写入入口，也没有按负载类型模板化的读写接口，因此不设
 *  `write_field` / 字段指针表 / `to_*` / `try_to_*`：取值闸门是"调用方按 `backend` 校验后直接读
 *  对应分支"，`None` 或未知值不得读任何分支。
 *
 *  不变量：
 *  1. 公开头不含 Windows / X11 / Wayland / Cocoa / Vulkan 的任何系统头；平台侧只以 `void*`
 *     与定宽整数出现（`NativeX11::x11_window` 是窗口 ID 数值，不是图形 API 句柄）。
 *  2. 布局断言按目标宽度表达：指针宽度随目标变化，`sizeof` 不钉死单一数值。
 *  3. 借用的有效期 = 对应窗口仍存活 且 运行时未 `shutdown` 且 模块未卸载，以三者最早发生者为止；
 *     复制本描述符不延长寿命。
 *  4. 本版只实现 Win32 分支：`Window::get_native_window_info` 在其它后端写日志并返回
 *     `NativeBackend::None` 的全零结果；X11/Wayland/Cocoa 的取值留待后续实现。
 */

namespace ID::Window
{
    enum class NativeBackend : std::uint8_t
    {
        None    = 0,
        Win32   = 1,
        X11     = 2,
        Wayland = 3,
        Cocoa   = 4,
    };

    struct NativeWin32
    {
        void* hwnd      = nullptr;      // Win32 窗口句柄（借用）
        void* hinstance = nullptr;      // Win32 实例句柄（借用）
    };

    struct NativeX11
    {
        void*         display    = nullptr;     // X11 display 连接（借用）
        std::uint64_t x11_window = 0;           // X11 窗口 ID 数值，不是指针
    };

    struct NativeWayland
    {
        void* display = nullptr;        // Wayland display 连接（借用）
        void* surface = nullptr;        // Wayland surface（借用）
    };

    struct NativeCocoa
    {
        void* ns_window = nullptr;      // Cocoa 窗口对象（借用）
    };

    struct alignas(8) NativeWindowInfo
    {
        NativeBackend backend = NativeBackend::None;

        union
        {
            NativeWin32     win32;
            NativeX11       x11;
            NativeWayland   wayland;
            NativeCocoa     cocoa;
        };

        constexpr NativeWindowInfo() noexcept
            : backend(NativeBackend::None), win32{}
        {
        }
    };

    namespace Detail
    {
        /**
         *  @brief union 内最大分支的字节数
         *  @return 四个原生产物分支 `sizeof` 的最大值
         *  @note 尺寸断言用它把"指针宽度随目标变化"表达成不依赖 ABI 的式子
         */
        constexpr std::size_t native_union_bytes() noexcept
        {
            std::size_t bytes = sizeof(NativeWin32);
            if (sizeof(NativeX11) > bytes)
            {
                bytes = sizeof(NativeX11);
            }
            if (sizeof(NativeWayland) > bytes)
            {
                bytes = sizeof(NativeWayland);
            }
            if (sizeof(NativeCocoa) > bytes)
            {
                bytes = sizeof(NativeCocoa);
            }
            return bytes;
        }

        inline constexpr std::size_t kNativeUnionBytes = native_union_bytes();
    } // namespace Detail

    static_assert(offsetof(NativeWindowInfo, backend) == 0, "backend 必须在偏移 0，否则取值闸门与调试视图失效");
    static_assert(alignof(NativeWindowInfo) == 8, "NativeWindowInfo 的对齐必须是 8：tag 占 1 字节后 union 起点对齐到 8");
    static_assert(std::is_standard_layout_v<NativeWindowInfo>, "标准布局是用 offsetof 做偏移断言的前提");
    static_assert(std::is_trivially_copyable_v<NativeWindowInfo>, "NativeWindowInfo 必须可平凡复制，否则不能跨模块按值传递");
    static_assert(sizeof(NativeWindowInfo) == 8 + ((Detail::kNativeUnionBytes + 7) / 8) * 8,
                  "NativeWindowInfo 的尺寸必须是 tag 区 8 字节加 union 分支宽度向上取整到 8；该形状在 32/64 位目标上都成立");
} // namespace ID::Window
