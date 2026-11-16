#pragma once

#include <cstdint>
#include <type_traits>

#include "IDWindow/KeyCode/KeyCode.hpp"
#include "IDWindow/Mouse.hpp"
#include "IDWindow/WindowPosition.hpp"
#include "IDWindow/WindowScale.hpp"
#include "IDWindow/WindowSize.hpp"

/**
 *  回调面向的事件负载：每种事件类型的一个"按类型视图"。
 *
 *  形态：全部是纯数据载体（`skills/cpp-style/references/design-patterns.md` 第 8 节）——
 *  只有公开字段 + NSDMI，无自定义构造函数、无成员函数、无虚函数；按值构造、按值传递。
 *  这些 POD 与 `EventContext`（`Event/EventContext.hpp`）之间的对应关系只在 `Window::on_event`
 *  的一处 `switch` 里建立，不散落到调用方。
 *
 *  不变量：
 *  1. 每个 POD 可平凡复制并保持标准布局：它们按值进入回调，不承担校验。
 *  2. 字段宽度不一致的 POD（例如 `KeyEvent` 的 `KeyCode` + `KeyAction`）允许尾部填充，
 *     因此**只钉可平凡复制与标准布局、不钉 `sizeof`**；无填充的纯数据 POD 各自在定义处钉大小。
 *  3. `Codepoint` 是独立值类型而不是 `std::uint32_t` 的别名：payload 判定按类型同一性工作，
 *     别名会让任意 `uint32` 被当成同一 payload。取值合法性只做入站校验（超出 Unicode scalar
 *     范围或落在 surrogate 区间即丢弃），类型本身不校验。
 *  4. `FileDropList::paths` 是**借用**指针：只在本次回调期间有效（UTF-8 字符串数组），
 *     拷贝 `FileDropEvent` 不延长寿命。
 *  5. `KeyAction` 的取值顺序与平台动作常量一致，但它是本插件自己的契约值。
 */
namespace ID::Window::Event
{
    /**
     *  @brief 按键动作
     */
    enum class KeyAction : std::uint8_t
    {
        Released = 0,
        Pressed  = 1,
        Repeat   = 2,
    };

    static_assert(sizeof(KeyAction) == sizeof(std::uint8_t), "KeyAction 必须是单字节枚举，否则事件 POD 布局契约失效");

    /**
     *  @brief 单个 Unicode scalar value（不是 UTF-8 串）
     */
    struct Codepoint
    {
        std::uint32_t value = 0;
    };

    static_assert(std::is_trivially_copyable_v<Codepoint>, "Codepoint 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<Codepoint>, "Codepoint 必须标准布局，否则字段偏移契约失效");
    static_assert(sizeof(Codepoint) == sizeof(std::uint32_t), "Codepoint 必须只有一个 32 位字段且无填充");
    static_assert(alignof(Codepoint) == alignof(std::uint32_t), "Codepoint 的对齐必须与 32 位整数一致");

    /**
     *  @brief 拖放文件列表（借用视图）
     */
    struct FileDropList
    {
        const char* const*  paths = nullptr;    // 借用：只在本次回调期间有效，UTF-8
        std::uint32_t       count = 0;
    };

    static_assert(std::is_trivially_copyable_v<FileDropList>, "FileDropList 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<FileDropList>, "FileDropList 必须标准布局，否则字段偏移契约失效");

    struct KeyEvent
    {
        KeyCode     key{};                                          // 已声明键码集合内的键
        KeyAction   action = KeyAction::Released;
    };

    struct MouseButtonEvent
    {
        KeyCode     button{};                                       // 鼠标键具名集合内的键
        KeyAction   action = KeyAction::Released;
    };

    struct CursorPosEvent
    {
        Mouse::Position position{};                                 // 相对窗口内容区左上角
    };

    struct ScrollEvent
    {
        Mouse::ScrollOffset offset{};                               // 本次滚动增量
    };

    struct CharEvent
    {
        Codepoint codepoint{};
    };

    struct WindowCloseEvent
    {
    };

    struct WindowSizeEvent
    {
        WindowSize size{};                                          // 逻辑尺寸或帧缓冲像素尺寸，由 EventType 区分
    };

    struct WindowPosEvent
    {
        WindowPosition position{};
    };

    struct WindowFocusEvent
    {
        bool is_focused = false;
    };

    struct WindowIconifyEvent
    {
        bool is_iconified = false;
    };

    struct WindowMaximizeEvent
    {
        bool is_maximized = false;
    };

    struct WindowRefreshEvent
    {
    };

    struct CursorEnterEvent
    {
        bool is_entered = false;
    };

    struct ContentScaleEvent
    {
        WindowScale scale{};
    };

    struct FileDropEvent
    {
        FileDropList drops{};
    };

    static_assert(std::is_trivially_copyable_v<KeyEvent>, "KeyEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<KeyEvent>, "KeyEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<MouseButtonEvent>, "MouseButtonEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<MouseButtonEvent>, "MouseButtonEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<CursorPosEvent>, "CursorPosEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<CursorPosEvent>, "CursorPosEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<ScrollEvent>, "ScrollEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<ScrollEvent>, "ScrollEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<CharEvent>, "CharEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<CharEvent>, "CharEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowCloseEvent>, "WindowCloseEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowCloseEvent>, "WindowCloseEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowSizeEvent>, "WindowSizeEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowSizeEvent>, "WindowSizeEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowPosEvent>, "WindowPosEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowPosEvent>, "WindowPosEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowFocusEvent>, "WindowFocusEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowFocusEvent>, "WindowFocusEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowIconifyEvent>, "WindowIconifyEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowIconifyEvent>, "WindowIconifyEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowMaximizeEvent>, "WindowMaximizeEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowMaximizeEvent>, "WindowMaximizeEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<WindowRefreshEvent>, "WindowRefreshEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<WindowRefreshEvent>, "WindowRefreshEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<CursorEnterEvent>, "CursorEnterEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<CursorEnterEvent>, "CursorEnterEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<ContentScaleEvent>, "ContentScaleEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<ContentScaleEvent>, "ContentScaleEvent 必须标准布局，否则字段偏移契约失效");
    static_assert(std::is_trivially_copyable_v<FileDropEvent>, "FileDropEvent 必须可平凡复制，否则不能按值进入回调");
    static_assert(std::is_standard_layout_v<FileDropEvent>, "FileDropEvent 必须标准布局，否则字段偏移契约失效");
} // namespace ID::Window::Event
