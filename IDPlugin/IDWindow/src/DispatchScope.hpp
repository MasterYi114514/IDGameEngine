#pragma once

/**
 *  派发深度与"回调栈内违约"判定的模块私有接口（不进公开 include 路径）。
 *
 *  本头只声明接口与作用域对象，全部状态与定义都在 `src/DispatchScope.cpp`：四个使用方的
 *  `.cpp` 都包含本头，因此同一接口只有一处声明，签名漂移在编译期暴露，不留到链接期。
 *
 *  不变量：
 *  1. `enter_dispatch()` / `leave_dispatch()` 必须成对配平：唯一派发点 `Window::on_event`
 *     通过 `DispatchScope` 的构造与析构配对调用，嵌套派发按栈累加深度。
 *  2. 深度是**每线程**的（`thread_local`）：它回答的只是"调用线程自己是否在事件回调栈内"。
 *     跨线程不共享，因此唯一允许跨线程的入口 `wake_event_loop()` 不会被主线程的回调栈误判，
 *     也不需要原子量或锁。
 *  3. 深度只用于识别"在事件回调栈内调用禁用接口"这一种违约（计划书 §7.5），
 *     不参与事件转发判定，也不决定事件是否投递。
 *  4. 违约只写日志与错误文本，不改任何现态：调用方据 `reject_if_in_callback()` 的返回值直接返回。
 */
namespace ID::Window::Detail
{
    /**
     *  @brief 派发深度加一
     *  @note 实现见 `src/DispatchScope.cpp`；只由 `DispatchScope` 的构造调用
     */
    void enter_dispatch() noexcept;

    /**
     *  @brief 派发深度减一
     *  @note 实现见 `src/DispatchScope.cpp`；热路径只做一次比较与自减，深度为 0 时才进冷函数
     *        记录断言日志且不减，不终止进程
     */
    void leave_dispatch() noexcept;

    /**
     *  @brief 判定本次调用是否发生在事件回调栈内
     *  @param function_name 被拒绝的接口名，只用于日志与错误文本；`nullptr` 时两者的该段都省略
     *  @return `true` 表示本线程处于回调栈内、本次调用必须被拒绝；`false` 表示可以继续
     *  @note 只看**调用线程**自己的深度：其它线程是否在派发中不影响本线程的判定
     *  @note 返回 `true` 时本函数已写好日志与错误文本，调用方不得改动任何现态
     */
    bool reject_if_in_callback(const char* function_name) noexcept;

    /**
     *  @brief 派发作用域：构造加一、析构减一，配平由 RAII 保证
     *  @note 不可复制、不交出地址、只在派发点作为局部对象存在
     */
    struct DispatchScope
    {
        DispatchScope() noexcept { enter_dispatch(); }
        ~DispatchScope() { leave_dispatch(); }

        DispatchScope(const DispatchScope&) = delete;
        DispatchScope& operator=(const DispatchScope&) = delete;
    };
} // namespace ID::Window::Detail
