#pragma once

#include <array>
#include <cstddef>
#include <tuple>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"
#include "Kernel/PluginCompileTime.hpp"

/**
 *  微内核插件门面（ID::PluginManager）与固定服务槽位（ID::ServiceSlot）
 *  1. `ServiceSlot` 是 64 槽服务表的唯一真源：运行期按这个枚举做一次固定下标寻址，
 *     枚举值属于跨插件 ABI，只能在末尾追加；`MaxSlots` 是边界哨兵，不是合法下标；
 *  2. `PluginManager` 不再是运行期单例，也不能实例化：它只有三个静态 `consteval` 操作族
 *     （`register_plugin` / `declare_dependencies` / `sort_and_validate`，实现见
 *     Kernel/PluginCompileTime.hpp）。它们是纯数据变换：不写任何全局状态，不存在跨编译单元的
 *     隐式注册表，调用位置、编译单元顺序与链接顺序都不构成状态；
 *  3. 旧的运行期入口（`instance()` / `register_plugin(const PluginDescriptor*)` /
 *     `initialize_all` / `shutdown_all` / 运行期 `get_service`/`register_service`）与运行期
 *     Kahn 拓扑排序已退役，不保留兼容包装或空实现；运行期职责唯一归属 `ID::PluginRuntime`
 *     （Kernel/PluginRuntime.hpp）：它接收一份已编译计划，只执行固定的四阶段生命周期；
 *  4. 本头因此只保留槽位词汇与编译期门面，没有描述符、没有回调指针、没有可变的槽位表。
 */

namespace ID
{
    // 编译期固定服务槽位（O(1) 单次数组下标寻址）
    enum class ServiceSlot : uint32_t
    {
        Log         = 0,
        Containers  = 1,
        Math        = 2,
        JobSystem   = 3,
        ECS         = 4,
        Physics     = 5,
        Render      = 6,
        Audio       = 7,
        Gameplay    = 8,
        MaxSlots    = 64
    };

    // 编译期组装的服务槽位上限必须与运行期槽位数量一致，否则槽位归属与函数表下标会分叉
    static_assert(PluginCompileTime::kMaxSlots == static_cast<size_t>(ServiceSlot::MaxSlots),
                  "PluginCompileTime::kMaxSlots 必须与 ServiceSlot::MaxSlots 一致");

    /**
     *  编译期插件门面：三个静态 `consteval` 操作族，没有运行期实例、没有状态。
     *  @note 不可实例化：构造/析构与拷贝均被删除，调用方只能写 `PluginManager::register_plugin(...)`
     *        这类静态调用；把门面当对象用属于编译错误，而不是运行期错误。
     */
    class PluginManager
    {
    public:
        static inline constexpr size_t kMaxPlugins = 128;                               // 产品选中插件数量上限
        static inline constexpr size_t kMaxSlots   = static_cast<size_t>(ServiceSlot::MaxSlots);
        // 编译期选中上限与门面常量必须同值：否则同一份计划在两侧的容量语义会分叉
        static_assert(PluginCompileTime::kMaxActive == kMaxPlugins,
                      "PluginCompileTime::kMaxActive 必须与 PluginManager::kMaxPlugins 一致");

        PluginManager()                                   = delete;
        ~PluginManager()                                  = delete;
        PluginManager(const PluginManager&)                = delete;
        PluginManager& operator=(const PluginManager&)     = delete;

        /**
         *  @brief 编译期注册一个插件身份（三个 consteval 操作族之一，计划书 5.2）
         *  @tparam N 字符串字面量/编译期字符数组的长度（含末尾 NUL）
         *  @param name 插件精确名：非空、<= 96 字节、首字符 ASCII 字母、其余为字母数字与 . _ -
         *  @return 拥有存储的 PluginIdentity（复制名字内容 + FNV-1a 64 身份）；格式非法时 is_valid 为假
         *  @note 只接受 `const char (&)[N]`：不接受运行期字符串，也不提供自定义哈希 ID 入口，
         *        因此不可能用“运行期改名”绕过组装期身份校验。
         *  @note 数组末尾必须是 '\0'；非终止数组返回 is_valid=false 的结构化无效身份，
         *        并保留最后一个输入字符供诊断，不会按 N-1 静默截断。
         *  @note 该名字是否在某个产品里启用，由 sort_and_validate 的显式选中集合决定；本操作不写任何全局状态。
         */
        template <size_t N>
        static consteval PluginCompileTime::PluginIdentity register_plugin(const char (&name)[N]) noexcept
        {
            return PluginCompileTime::detail::make_identity_literal(name);
        }

        /**
         *  @brief 声明依赖：外层 AND、内层 OR 的不等长二维编译期名字清单（计划书 5.3）
         *  @tparam Identity 所属插件身份类型
         *  @tparam Groups 每组候选的数组类型（必须显式指定元素类型，避免自动推导成 const char*）
         *  @param identity 所属插件身份，必须是 register_plugin 的返回值
         *  @param groups 每组的候选名清单；空 tuple 表示没有上游
         *  @return 拥有存储的 PluginDeclaration<组数, 候选总数>；空内层、组内重复、自依赖、非法名字记进 error
         *  @note 名字字节复制进声明自己的字节池，不保存指向调用者缓冲区的指针。
         */
        template <typename Identity, typename... Groups>
        static consteval auto declare_dependencies(Identity identity, const std::tuple<Groups...>& groups) noexcept
            -> PluginCompileTime::PluginDeclaration<sizeof...(Groups),
                                                    (PluginCompileTime::detail::group_size<Groups>::value + ... + 0)>
        {
            return PluginCompileTime::detail::declare_tuple(identity, groups);
        }

        /**
         *  @brief 声明依赖的矩形便捷重载：G 个需求组、每组恰好 K 个候选
         *  @param groups array<array<元素, K>, G>；G 或 K 为 0 时同样成立（显式给出元素类型的空数组）
         *  @return 拥有存储的 PluginDeclaration<G, G*K>
         *  @note 语义与 tuple 形态完全一致，属于同一个操作族，不是并列入口。
         */
        template <typename Identity, typename Element, size_t Candidates, size_t Groups>
        static consteval auto declare_dependencies(
            Identity identity, const std::array<std::array<Element, Candidates>, Groups>& groups) noexcept
            -> PluginCompileTime::PluginDeclaration<Groups, Groups * Candidates>
        {
            return PluginCompileTime::detail::declare_rect(identity, groups);
        }

        /**
         *  @brief 绑定、建图、排序与静态校验（三个 consteval 操作族之一，计划书 5.4 / 6）
         *  @tparam Shape 输入容量形状（catalog 类型、active 容量与各类证据容量）
         *  @param input 完整 catalog + 本产品显式选中的身份 + 构建系统派生的证据
         *  @return AssemblyResult<Shape>；成功时含规范化 active 表、catalog 映射、固定拓扑序、逐需求 provider、
         *          去重边、槽位归属、能力/接口修订元数据与确定性指纹
         *  @note 纯函数：不写任何全局状态；catalog 顺序、选中输入顺序、候选顺序都不影响绑定与顺序。
         *        错误结果不得进入运行期绑定表，错误类别与阶段见 AssemblyDiagnostic。
         */
        template <typename Shape>
        static consteval PluginCompileTime::AssemblyResult<Shape> sort_and_validate(
            const PluginCompileTime::AssemblyInput<Shape>& input) noexcept
        {
            return PluginCompileTime::detail::sort_dispatch<Shape>(input);
        }
    };
} // namespace ID
