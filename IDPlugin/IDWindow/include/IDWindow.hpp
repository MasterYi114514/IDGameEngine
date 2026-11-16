#pragma once

#include "IDWindowCore.hpp"

/**
 *  IDWindow 的聚合入口：进程级生命周期与事件泵。
 *
 *  本插件把 vendor GLFW 3.5.1 静态包在内部，只对外暴露"平台与窗口子系统是否就绪"这一层契约：
 *  GLFW 的类型、头文件与原生句柄都不出现在公开头里，消费者不感知底层实现，也不需要 GLFW 的
 *  include 路径或链接项。窗口对象与事件层分别见 `IDWindow/WindowPool.hpp` 与
 *  `IDWindow/Event/EventManager.hpp`；本头不做全量伞包含。
 *
 *  就绪条件与顺序：
 *  - `init()` 之前 `is_initialized()` 为 false；`init()` 可以失败，失败原因见 `last_error()`。
 *  - `init()` / `shutdown()` 必须由主线程串行调用：底层要求 GUI 侧调用留在主线程，而且第一次
 *    `init()` 所在的线程必须就是平台主线程（这是调用方前置条件，插件无法校验）。
 *  - 若希望错误文本同时进入日志，必须先 `ID::Log::init()` 并保证 IDLog 生命周期覆盖 IDWindow；
 *    IDLog 未初始化导致本模块日志器创建失败时 `init()` 返回 `false`，错误文本照常可从
 *    `last_error()` 取得；日志只决定诊断通道是否可用，不改变成功路径的返回值。
 *
 *  失败语义：
 *  - 可恢复失败一律以返回值 / 错误文本表达，不抛异常、不终止进程；本插件没有错误码枚举。
 *  - `last_error()` 返回最近一次错误文本；从未收到错误时为空串。成功不会清理既有文本。
 *  - 热路径（事件转发、窗口操作与查询、GL 交接）不写日志；诊断只落在失败冷路径。
 */
namespace ID::Window
{
    /**
     *  @brief 取本插件的版本身份串
     *  @return 由版本宏在编译期拼接的静态字符串（形如 `"IDWindow 0.1 (GLFW 3.5.1)"`）；
     *          同一模块加载周期内地址与内容不变
     *  @note 不分配、不格式化、不加锁
     */
    ID_WINDOW_API const char* version_string() noexcept;

    /**
     *  @brief 初始化窗口子系统：装平台错误回调 → 平台进程级初始化 → 复位窗口池与事件层
     *  @return 成功或已处于已初始化状态时返回 true；失败返回 false，失败原因写入 `last_error()`
     *  @note 幂等：已初始化时直接返回 true，不重复初始化也不会清空 `last_error()`
     *  @note 失败时按已建立项逆序回滚，保持"未初始化"状态，不留下部分就绪的子系统；之后可再次调用。
     *        日志器创建失败时只写错误文本（此时没有任何日志通道）；平台进程级初始化失败时依次撤销
     *        平台错误回调与本插件自己的日志器句柄，`ID::Log` 本身不归本插件关闭
     *  @note 只允许主线程调用；与 `shutdown()` 不得并发
     */
    ID_WINDOW_API bool init();

    /**
     *  @brief 关闭窗口子系统：销毁全部存活窗口 → 关闭事件层 → 销毁本插件自己的日志器句柄 →
     *         平台进程级终止 → 复位槽表与 current
     *  @note 幂等：未初始化时是无操作；之后可以再次 `init()`
     *  @note 只允许主线程调用；返回后所有句柄与原 `Window&` 失效
     *  @note 不调用 `ID::Log::shutdown()`：日志插件可能被其它插件共用，插件不关闭别人的日志系统
     */
    ID_WINDOW_API void shutdown() noexcept;

    /**
     *  @brief 查询当前是否处于已初始化状态
     *  @return 已成功 `init()` 且尚未 `shutdown()` 时返回 true
     *  @note 只观察原子标志，可在任意线程调用；状态变更仍由主线程的 `init()` / `shutdown()` 驱动
     */
    ID_WINDOW_API bool is_initialized() noexcept;

    /**
     *  @brief 取本线程最近一次失败文本
     *  @return 以 `'\0'` 结尾的窄字符串，非 `nullptr`；本线程从未失败时为空串
     *  @note 由本线程的定长缓冲承载，写入只做定长字节拷贝（不格式化、不取日志器）：
     *        日志器未创建、已销毁或 IDLog 未初始化时该通道照常可用
     *  @note 返回的指针只在同一线程的下一次失败写入或线程退出前有效；需要长期保存须自行拷贝
     *  @note 成功路径不清空该文本；空串只表示"本线程还没有收到过错误"
     *  @note 缓冲是定长的：文本超过 511 字节时被截断，函数不返回长度，调用方不应假定拿到完整原文
     */
    ID_WINDOW_API const char* last_error() noexcept;

    // ========== 进程级事件泵 ==========

    /**
     *  @brief 处理所有已发生的事件（不阻塞）
     *  @note 只是转发生效的平台事件泵：事件在各自的平台回调栈内投递给产生它的窗口
     *  @note 未初始化时写日志、不转发；在回调栈内再次泵属调用方违约，同样只写日志
     *  @note 只允许主线程调用
     */
    ID_WINDOW_API void poll_events() noexcept;

    /**
     *  @brief 阻塞等待事件，超时返回（不忙轮询）
     *  @param timeout_seconds 超时秒数，必须为有限正数；≤ 0 / NaN / ∞ 属调用方违约（写日志、不等待）
     *  @note 只允许主线程调用；回调栈内调用属违约
     */
    ID_WINDOW_API void wait_events(double timeout_seconds) noexcept;

    /**
     *  @brief 投递一个空事件，唤醒正在等待的事件循环
     *  @note 这是唯一允许跨线程调用的入口；`shutdown()` 之前必须停掉生产者线程
     *  @note 回调栈内违约的判定只看**调用线程自己**的派发栈：主线程正在派发时，后台线程调用本接口不会被误拒
     *  @note 未初始化时写日志、不转发
     */
    ID_WINDOW_API void wake_event_loop() noexcept;
} // namespace ID::Window
