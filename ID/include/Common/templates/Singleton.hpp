#pragma once

#include "Common/Core.hpp"

/**
 *  通过 CRTP 的方法实现的单例模式
 *  这里提供以下两种单例模式的实现：
 *  1. 饿汉式单例模式（Eager Singleton）：在编译期（constinit）直接完成静态数据段初始化，零运行时构造开销、零分支检查开销，适用于高频访问或底层核心场景。
 *  2. 懒汉式单例模式（Lazy Singleton）：在第一次访问实例时才创建，适用于模块内部创建开销较大且不一定会被使用的非热路径场景。
 * 
 *  注意：
 *  1. SingletonEager 适用于跨 DLL 导出的核心单例，s_instance 可在 .cpp 中显式特化定义，保证所有模块/插件共享同一实例。
 *  2. SingletonLazy 仅适用于单模块/插件内部使用，跨 DLL 请勿直接在头文件内联 static 局部变量，否则会导致每个模块/插件各自拥有独立的实例。
 */

namespace ID
{
    // 饿汉式模板（强制 constinit 编译期初始化，彻底杜绝静态初始化顺序灾难 SIOF）
    template <typename T>
    class SingletonEager
    {
    protected:
        constexpr SingletonEager() noexcept = default;
        ~SingletonEager() noexcept = default;

    public:
        ID_FORCEINLINE static T& get_instance() noexcept
        {
            return s_instance;
        }

        ID_FORCEINLINE static T& instance() noexcept
        {
            return s_instance;
        }

        SingletonEager(const SingletonEager&) = delete;
        SingletonEager& operator=(const SingletonEager&) = delete;

    private:
        static T s_instance;
    };

    // 默认静态定义（适用于单模块内部或 Monolithic 静态 LTO 模式；跨 DLL 导出的核心单例可通过特化 s_instance 驻留在 .cpp 中）
    template <typename T>
    inline constinit T SingletonEager<T>::s_instance{};

    // 懒汉式模板（注意：仅限单模块/插件内部使用，跨 DLL 请勿直接在头文件内联 static 局部变量）
    template <typename T>
    class SingletonLazy
    {
    protected:
        SingletonLazy() = default;
        ~SingletonLazy() = default;

    public:
        static T& get_instance() noexcept(noexcept(T()))
        {
            static T s_instance;
            return s_instance;
        }

        static T& instance() noexcept(noexcept(T()))
        {
            return get_instance();
        }

        SingletonLazy(const SingletonLazy&) = delete;
        SingletonLazy& operator=(const SingletonLazy&) = delete;
    };
} // namespace ID
