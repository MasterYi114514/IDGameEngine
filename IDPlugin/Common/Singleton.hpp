#pragma once

#include <concepts>

#include "Core/Core.hpp"

/**
 *  通过 CRTP 的方法实现的单例模式
 *  这里提供以下两种单例模式的实现：
 *  1. 饿汉式单例模式（Eager Singleton）：在进入 main 之前就静态初始化完成，注意静态初始化的顺序问题
 *  2. 懒汉式单例模式（Lazy Singleton）：在第一次访问实例时才创建，适用于模块内部创建开销较大且不一定会被使用的非热路径场景。
 * 
 *  注意：
 *  Singleton 模板仅适用于单模块/插件内部使用，跨 DLL 请勿直接在头文件内联 static 局部变量，否则会导致每个模块/插件各自拥有独立的实例。
 */

namespace ID::Common
{
    /**
     *  @brief 饿汉式单例模式模板类。该类通过 CRTP（Curiously Recurring Template Pattern）实现单例模式，确保派生类只有唯一实例。
     *  @tparam T 派生类类型，必须继承自 SingletonEager<T>。
     *  @note 子类（class T : public SingletonEager<T>）需要将 SingletonEager<T> 声明为友元类
     */
    template <typename T>
    class SingletonEager
    {
    protected:
        constexpr SingletonEager() noexcept = default;
        ~SingletonEager() noexcept = default;

    public:
        ID_FORCEINLINE static T& get_instance() noexcept
        {
            return T::s_instance;
        }

        ID_FORCEINLINE static T& instance() noexcept
        {
            return T::s_instance;
        }

        SingletonEager(const SingletonEager&) = delete;
        SingletonEager& operator=(const SingletonEager&) = delete;

    private:
        static T s_instance;
    };

    template <typename T>
    T SingletonEager<T>::s_instance;    // 静态成员变量定义

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
} // namespace ID::Common