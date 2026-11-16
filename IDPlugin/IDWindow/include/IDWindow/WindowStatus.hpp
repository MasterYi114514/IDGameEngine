#pragma once

#include <cstdint>
#include <type_traits>

/**
 *  单个窗口的当前状态位标志（**不是错误码**，不表示调用成功/失败）。
 *
 *  形态：位标志枚举（名字）+ 位集合包装类（组合值的载体，按
 *  `skills/cpp-style/references/design-patterns.md` 第 2 节）。
 *  维护清单：新增一个状态位时必须同步维护
 *      1. `WindowStatusEnum` 的位值与底层宽度余量（`IDWindowStatusUINT` 目前是单字节）
 *      2. `Window::get_status` 里对应的平台属性取值
 *      3. 本文件末尾的三条 `static_assert`（宽度不足时另案调整底层别名）
 *
 *  不变量：
 *  1. 包装类只有位集合字一个成员，无缓存、无其它状态。
 *  2. 判定只用 `is_none()` 与 `is_in_status()`，调用方不写裸位运算。
 *  3. 裸位构造是 `explicit`：整数不会被静默当成状态集合。
 */

namespace ID::Window
{
    using IDWindowStatusUINT = std::uint8_t;

    enum class WindowStatusEnum : IDWindowStatusUINT
    {
        None        = 0,
        Visible     = 1u << 0,       // 对应平台 VISIBLE 属性
        Focused     = 1u << 1,       // 对应平台 FOCUSED 属性
        Iconified   = 1u << 2,       // 对应平台 ICONIFIED 属性
        Maximized   = 1u << 3,       // 对应平台 MAXIMIZED 属性
        Resizable   = 1u << 4,       // 对应平台 RESIZABLE 属性
        Decorated   = 1u << 5,       // 对应平台 DECORATED 属性
        ShouldClose = 1u << 6,       // 对应平台的关闭请求标志
    };

    /**
     *  @brief 单个窗口的状态位集合
     *  @note 只描述"这个窗口现在是什么状态"，不承载成功/失败语义
     */
    class WindowStatus
    {
    public:
        constexpr WindowStatus() noexcept
            : m_status_bits(kNone)
        {
        }

        /**
         *  @brief 用裸位集合构造
         *  @param bits 位集合字；合法取值是 `WindowStatusEnum` 各单值的按位或，本构造函数不做校验
         *  @note 显式构造：禁止整数被静默当成状态集合
         */
        explicit constexpr WindowStatus(IDWindowStatusUINT bits) noexcept
            : m_status_bits(bits)
        {
        }

        constexpr WindowStatus(WindowStatusEnum status) noexcept
            : m_status_bits(static_cast<IDWindowStatusUINT>(status))
        {
        }

        constexpr WindowStatus(const WindowStatus& other) noexcept = default;
        constexpr WindowStatus& operator=(const WindowStatus& other) noexcept = default;
        constexpr WindowStatus(WindowStatus&& other) noexcept = default;
        constexpr WindowStatus& operator=(WindowStatus&& other) noexcept = default;

        constexpr bool is_none() const noexcept
        {
            return m_status_bits == kNone;
        }

        constexpr bool is_in_status(WindowStatusEnum status) const noexcept
        {
            return (m_status_bits & static_cast<IDWindowStatusUINT>(status)) != 0;
        }

    private:
        static inline constexpr IDWindowStatusUINT kNone = static_cast<IDWindowStatusUINT>(WindowStatusEnum::None);

        IDWindowStatusUINT m_status_bits;        // 位集合字；空状态为 0
    };

    static_assert(sizeof(WindowStatusEnum) == sizeof(IDWindowStatusUINT), "WindowStatusEnum 的底层类型必须与位集合字同宽");
    static_assert(sizeof(WindowStatus) == sizeof(IDWindowStatusUINT), "WindowStatus 必须是单个位集合字，不能带其它状态");
    static_assert(std::is_trivially_copyable_v<WindowStatus>, "WindowStatus 必须可平凡复制，否则不能跨模块按值传递");

    constexpr WindowStatus operator|(WindowStatusEnum lhs, WindowStatusEnum rhs) noexcept
    {
        const IDWindowStatusUINT bits = static_cast<IDWindowStatusUINT>(lhs) | static_cast<IDWindowStatusUINT>(rhs);
        return WindowStatus{bits};
    }
} // namespace ID::Window
