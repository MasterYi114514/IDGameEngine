#pragma once

#include "Core/Core.hpp"

#include "IDWindow/KeyCode/KeyCode.hpp"
#include "IDWindow/KeyCode/KeyCodes.hpp"

/**
 *  修饰键判定：只判断"这个键本身是不是修饰键"，不维护按下状态。
 *
 *  不变量：
 *  1. 全是 `ID_FORCEINLINE constexpr noexcept` 的值类型谓词：既可在运行期按事件载荷调用，
 *     也可在编译期用于静态检查。
 *  2. 参数取平铺层的 `KeyCode`，常量取 `ID::Window::KeyCodes`；本头不复制键值数值。
 *  3. 判定只看键码本身：区分左右修饰键、不合成"修饰键组合"状态。
 */

namespace ID::Window::Keyboard
{
    /**
     *  @brief 该键本身是否为 Ctrl
     *  @param key 待判断的键码
     *  @return 左 Ctrl 或右 Ctrl 时返回 true
     */
    ID_FORCEINLINE constexpr bool is_ctrl(KeyCode key) noexcept
    {
        return key == KeyCodes::LeftControl || key == KeyCodes::RightControl;
    }

    /**
     *  @brief 该键本身是否为 Shift
     *  @param key 待判断的键码
     *  @return 左 Shift 或右 Shift 时返回 true
     */
    ID_FORCEINLINE constexpr bool is_shift(KeyCode key) noexcept
    {
        return key == KeyCodes::LeftShift || key == KeyCodes::RightShift;
    }

    /**
     *  @brief 该键本身是否为 Alt
     *  @param key 待判断的键码
     *  @return 左 Alt 或右 Alt 时返回 true
     */
    ID_FORCEINLINE constexpr bool is_alt(KeyCode key) noexcept
    {
        return key == KeyCodes::LeftAlt || key == KeyCodes::RightAlt;
    }

    /**
     *  @brief 该键本身是否为 Super（Windows 键 / Command 键）
     *  @param key 待判断的键码
     *  @return 左 Super 或右 Super 时返回 true
     */
    ID_FORCEINLINE constexpr bool is_super(KeyCode key) noexcept
    {
        return key == KeyCodes::LeftSuper || key == KeyCodes::RightSuper;
    }
} // namespace ID::Window::Keyboard
