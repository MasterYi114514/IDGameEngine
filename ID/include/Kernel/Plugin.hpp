#pragma once

#include <span>
#include <string_view>

#include "Common/Core.hpp"
#include "Common/templates/Singleton.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  微内核插件管理系统与固定槽位服务注册表（ID::PluginManager）
 *  1. 提供 O(1) 单条数组索引指令寻址的 ServiceSlot 服务表，彻底消除跨插件虚表 (vtable) 与字符串哈希查找开销；
 *  2. 支持“一槽多后端”兼容白名单（compatible_ids），下游插件可声明支持多个上游插件实现，不兼容时在组装阶段直接拦截；
 *  3. 基于 Kahn 拓扑排序算法解析插件间的有向无环图 (DAG) 依赖，自动检测循环依赖并按严格对称顺序调度生命周期。
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

    // 插件依赖项声明结构（支持单一精确依赖与多上游兼容白名单两种写法）
    struct PluginDependency
    {
        uint64_t                    plugin_id       = 0;        // 精确依赖的单个目标插件 ID（当 compatible_ids 为空时生效）
        uint32_t                    min_version     = 1;        // 要求目标插件满足的最低版本号
        std::span<const uint64_t>   compatible_ids  = {};       // 兼容的上游插件 ID 白名单列表（只要当前已注册其中一个且版本达标即可组装）
        ServiceSlot                 slot            = ServiceSlot::MaxSlots; // 该依赖对应的抽象功能槽位（可选，用于诊断报错提示）

        constexpr PluginDependency() noexcept = default;

        // 构造方式 1：依赖单一指定插件 ID
        constexpr PluginDependency(uint64_t id, uint32_t ver = 1, ServiceSlot s = ServiceSlot::MaxSlots) noexcept
            : plugin_id(id), min_version(ver), compatible_ids{}, slot(s) { }

        // 构造方式 2：依赖多个可替换上游插件的兼容白名单（如同时兼容 IDMath_SIMD 与 IDMath_Fixed）
        constexpr PluginDependency(std::span<const uint64_t> compat_list, uint32_t ver = 1, ServiceSlot s = ServiceSlot::MaxSlots) noexcept
            : plugin_id(0), min_version(ver), compatible_ids(compat_list), slot(s) { }

        // 判断给定的上游插件 ID 是否在本依赖的兼容范围内
        ID_FORCEINLINE constexpr bool matches(uint64_t candidate_id) const noexcept
        {
            if (!compatible_ids.empty())
            {
                for (uint64_t id : compatible_ids)
                {
                    if (id == candidate_id)
                    {
                        return true;
                    }
                }
                return false;
            }
            return plugin_id == candidate_id;
        }
    };

    // 插件静态描述清单（每个插件仅需导出一个 constexpr 描述符实例）
    struct PluginDescriptor
    {
        uint64_t                            plugin_id       = 0;        // 插件唯一编译期 ID（通常由 hash_fnv_1a64("ID.XXX") 生成）
        uint32_t                            version         = 1;        // 插件当前版本号
        std::string_view                    name            = {};       // 插件可读名称
        std::span<const PluginDependency>   dependencies    = {};       // 依赖的前置插件列表

        void(*on_register)() noexcept   = nullptr;  // 阶段 1 (拓扑正序)：注册 TypeID、内存池规格与 ServiceSlot 函数指针表
        bool(*on_init)() noexcept       = nullptr;  // 阶段 2 (拓扑正序)：所有前置依赖插件均已就绪，初始化本插件子系统
        void(*on_shutdown)() noexcept   = nullptr;  // 阶段 3 (拓扑逆序)：停止后台任务，安全释放子系统运行期资源
        void(*on_unload)() noexcept     = nullptr;  // 阶段 4 (拓扑逆序)：从 ServiceSlot 注销 API 函数指针表
    };

    // 微内核插件与服务管理器单例
    class alignas(kCacheLineSize) ID_KERNEL_API PluginManager : public SingletonEager<PluginManager>
    {
        friend class SingletonEager<PluginManager>;

    public:
        static inline constexpr size_t kMaxPlugins = 128;                               // 引擎支持的最大插件数量上限
        static inline constexpr size_t kMaxSlots   = static_cast<size_t>(ServiceSlot::MaxSlots);

    protected:
        constexpr PluginManager() noexcept = default;
        ~PluginManager() noexcept = default;

    public:
        // 注册或注销固定槽位服务表（O(1) 零开销写入）
        ID_FORCEINLINE void register_service(ServiceSlot slot, const void* api_table) noexcept
        {
            m_service_slots[static_cast<size_t>(slot)] = api_table;
        }

        // 获取指定槽位的服务函数表指针（编译期展开为 1 条数组偏移读取指令）
        template <typename ApiTableType>
        ID_FORCEINLINE const ApiTableType* get_service(ServiceSlot slot) const noexcept
        {
            return static_cast<const ApiTableType*>(m_service_slots[static_cast<size_t>(slot)]);
        }

        // 向微内核注册插件描述符（必须在 initialize_all 之前调用）
        bool register_plugin(const PluginDescriptor* descriptor) noexcept;

        // 执行兼容白名单校验、Kahn 拓扑排序、环路检测，并按拓扑正序依次执行 on_register -> on_init
        bool initialize_all() noexcept;

        // 按严格拓扑逆序依次执行 on_shutdown -> on_unload
        void shutdown_all() noexcept;

        ID_FORCEINLINE size_t registered_count() const noexcept
        {
            return m_registered_count;
        }

        ID_FORCEINLINE std::span<const PluginDescriptor* const> topological_order() const noexcept
        {
            return { m_topo_order, m_initialized_count };
        }

    private:
        int32_t find_plugin_index(uint64_t plugin_id) const noexcept;
        int32_t find_compatible_index(const PluginDependency& dep) const noexcept;

    private:
        static PluginManager s_instance;             // 唯一实例

        const void*             m_service_slots[kMaxSlots]      = { nullptr };  // 固定槽位全局服务指针数组
        const PluginDescriptor* m_registered[kMaxPlugins]       = { nullptr };  // 原始注册的插件描述符列表
        const PluginDescriptor* m_topo_order[kMaxPlugins]       = { nullptr };  // 经 Kahn 拓扑排序后的有序插件列表
        size_t                  m_registered_count              = 0;            // 已注册插件数量
        size_t                  m_initialized_count             = 0;            // 已成功完成 on_init 的插件数量
        bool                    m_is_initialized                = false;        // 系统是否已完成整体初始化
    };
} // namespace ID
