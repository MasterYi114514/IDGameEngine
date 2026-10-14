#pragma once

#include <span>
#include <string_view>

#include "Common/Core.hpp"
#include "Common/templates/Singleton.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  微内核插件管理系统与固定槽位服务注册表（ID::PluginManager）
 *  1. 提供 O(1) 单条数组索引指令寻址的 ServiceSlot 服务表，彻底消除跨插件虚表 (vtable) 与字符串哈希查找开销；
 *  2. 基于 Kahn 拓扑排序算法解析插件间的有向无环图 (DAG) 依赖，自动检测循环依赖并按严格对称顺序调度生命周期。
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

    // 插件依赖项声明结构
    struct PluginDependency
    {
        uint64_t    plugin_id   = 0;    // 依赖的目标插件编译期哈希 ID
        uint32_t    min_version = 1;    // 要求目标插件满足的最低版本号
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

        // 执行 Kahn 拓扑排序、环路检测，并按拓扑正序依次执行 on_register -> on_init
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

    private:
        const void*             m_service_slots[kMaxSlots]      = { nullptr };  // 固定槽位全局服务指针数组
        const PluginDescriptor* m_registered[kMaxPlugins]       = { nullptr };  // 原始注册的插件描述符列表
        const PluginDescriptor* m_topo_order[kMaxPlugins]       = { nullptr };  // 经 Kahn 拓扑排序后的有序插件列表
        size_t                  m_registered_count              = 0;            // 已注册插件数量
        size_t                  m_initialized_count             = 0;            // 已成功完成 on_init 的插件数量
        bool                    m_is_initialized                = false;        // 系统是否已完成整体初始化
    };

    // 显式特化声明：将 PluginManager 单例静态成员导出在微内核 DLL 中，确保跨所有插件全局唯一
    template <>
    ID_KERNEL_API PluginManager SingletonEager<PluginManager>::s_instance;
} // namespace ID
