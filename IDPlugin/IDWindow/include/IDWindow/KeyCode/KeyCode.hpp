#pragma once

#include <cstdint>
#include <type_traits>

/**
 *  键码值类型：键盘键与鼠标键共用的稳定标识。
 *
 *  落位说明（命名特例，理由见实施计划书 §3.3）：`KeyCode` 与底层宽度别名
 *  `IDKeyCodeUnderlyingType` **平铺在 `ID::Window`**，具名常量在 `ID::Window::KeyCodes`。
 *  不设名为 `KeyCode` 的二级命名空间：事件 POD 的字段与 `Window::get_key_state` 一类参数都裸写
 *  `KeyCode`，同名命名空间会被名字查找先命中，类型用法随之编不过。
 *
 *  不变量：
 *  1. 只有 2 字节，按值传递即可，不需要传引用或指针。
 *  2. 无效值是底层类型的最大值；`is_valid()` 只排除无效值，不做取值范围校验。
 *  3. 值域属于本插件自己的稳定值域，跨模块按数值比较是允许的，但不要假设它等于任何 vendor 宏。
 */

namespace ID::Window
{
    using IDKeyCodeUnderlyingType = std::uint16_t;

    /**
     *  @brief 键码的稳定标识
     *  @note 只有 2 字节，按值传递即可
     */
    class KeyCode
    {
    public:
        /**
         *  @brief 构造无效键码
         */
        constexpr KeyCode() noexcept
            : m_key(kInvalidKey)
        {
        }

        /**
         *  @brief 用底层数值构造
         *  @param key 底层键值；合法取值见 `ID::Window::KeyCodes` 的具名常量与域常量，本构造函数不做校验
         *  @note 显式构造：禁止整数被静默当成键码
         */
        explicit constexpr KeyCode(IDKeyCodeUnderlyingType key) noexcept
            : m_key(key)
        {
        }

        constexpr KeyCode(const KeyCode& other) noexcept = default;
        constexpr KeyCode& operator=(const KeyCode& other) noexcept = default;
        constexpr KeyCode(KeyCode&& other) noexcept = default;
        constexpr KeyCode& operator=(KeyCode&& other) noexcept = default;

        /**
         *  @brief 取无效键码
         *  @return 底层数值为最大值（`std::uint16_t` 全 1）的 `KeyCode`
         */
        static constexpr KeyCode invalid_keycode() noexcept
        {
            return KeyCode{kInvalidKey};
        }

        /**
         *  @brief 是否表示一个真实键值
         *  @return 不是无效值时返回 true
         */
        constexpr bool is_valid() const noexcept
        {
            return m_key != kInvalidKey;
        }

        /**
         *  @brief 取底层数值
         *  @return 2 字节键值；无效值为最大值
         */
        constexpr IDKeyCodeUnderlyingType get_key() const noexcept
        {
            return m_key;
        }

        constexpr bool operator==(const KeyCode& other) const noexcept
        {
            return m_key == other.m_key;
        }

        constexpr bool operator!=(const KeyCode& other) const noexcept
        {
            return m_key != other.m_key;
        }

    private:
        static inline constexpr IDKeyCodeUnderlyingType kInvalidKey = static_cast<IDKeyCodeUnderlyingType>(-1);

        IDKeyCodeUnderlyingType m_key;      // 键值；无效值为最大值
    };

    static_assert(sizeof(KeyCode) == sizeof(IDKeyCodeUnderlyingType), "KeyCode 必须与底层类型同宽，否则按值传递与布局契约失效");
    static_assert(std::is_trivially_copyable_v<KeyCode>, "KeyCode 必须可平凡复制，否则不能跨模块按值传递");
} // namespace ID::Window
