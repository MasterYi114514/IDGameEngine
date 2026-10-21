#pragma once

#include "FixturePluginApi.hpp"

/**
 *  仅测试/生成器使用的构造入口（不属于三个公共操作族）
 *  1. make_colliding_identity：直接构造"名字与 ID 不一致"的身份，用于模拟哈希碰撞；
 *     生产 register_plugin 永远计算真实哈希，绝不提供这样的公共入口（计划书 16.1 的模拟规则）；
 *  2. make_identity_from_text：给按数值生成名字的压力输入使用。
 */

namespace ID::Bench::Fixture::TestSupport
{
    /**
     *  @brief 构造一个名字合法、但 ID 被强制指定的身份
     *  @param name 合法的编译期字符数组
     *  @param forced_id 期望强制写入的 64 位 ID
     *  @return 拥有存储的身份，名字来自 name，ID 来自 forced_id
     *  @note 只用于验证"不同名同哈希"必须被诊断为 NameHashCollision
     */
    template <size_t N>
    consteval PluginIdentity make_colliding_identity(const char (&name)[N], uint64_t forced_id) noexcept
    {
        PluginIdentity identity = detail::identity_from_chars(name, N - 1);
        identity.id             = forced_id;
        return identity;
    }

    /**
     *  @brief 从字符缓冲建立身份（压力输入使用）
     *  @param text 字符起始地址
     *  @param length 名字字节数（不含末尾 NUL）
     *  @return 与 register_plugin 相同的拥有存储身份
     */
    consteval PluginIdentity make_identity_from_text(const char* text, size_t length) noexcept
    {
        return detail::identity_from_chars(text, length);
    }

    /**
     *  @brief 把某个候选的 64 位 ID 强制改成指定值（仅测试夹具使用）
     *  @tparam Declaration PluginDeclaration<G, C>
     *  @param declaration 目标声明（按引用修改）
     *  @param index 候选下标，必须小于 candidate_count
     *  @param forced_id 强制写入的 ID
     *  @note 用来构造"前 16 字节相同 + 哈希相同 + 完整名字不同"的碰撞输入，验证
     *        "同哈希不同名不得被判成自依赖"。生产 register_plugin 永远按名字计算真实哈希，
     *        不存在该入口，也不新增公共自定义 ID 接口（计划书 16.1 的模拟规则）。
     */
    template <typename Declaration>
    consteval void force_candidate_id(Declaration& declaration, size_t index, uint64_t forced_id) noexcept
    {
        declaration.candidate_id[index] = forced_id;
    }
} // namespace ID::Bench::Fixture::TestSupport
