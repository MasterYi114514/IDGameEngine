#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "Scene/GameObject.hpp"

namespace ID
{
    /**
     *  @brief 组件工厂：按类型名创建组件并完成反序列化
     *
     *  Creator 直接接收 (GameObject&, const Json&)，内部完成组件入池
     *  （经 GameObject::adopt_component）与字段反序列化，保持与旧实现
     *  一致的顺序：构造 → deserialize(json) → on_attach（AudioSourceComponent
     *  等组件依赖此顺序传递 pending 激活状态）。
     */
    class ID_API ComponentFactory
    {
    public:
        /// 创建并挂载一个组件：入池 + 反序列化；重复类型 / 未注册返回 false
        using ComponentCreator = std::function<bool(GameObject&, const Json&)>;

        static bool register_component(const std::string& name, ComponentCreator creator);

        static bool create(const std::string& name, GameObject& game_object, const Json& json);
        static bool is_registered(const std::string& name);

        /**
         *  @brief 通用 Creator 实现（ID_REGISTER_COMPONENT 宏使用）
         *
         *  单实例约束（含 AudioSourceComponent 池化后的单实例退化）：
         *  owner 已持有该类型组件时返回 false，跳过旧存档中的重复条目。
         */
        template<typename ComponentType>
        static bool default_creator(GameObject& game_object, const Json& json)
        {
            static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

            if (game_object.has_component<ComponentType>())
            {
                report_duplicate_component(game_object.get_name());
                return false;
            }

            auto component = std::make_unique<ComponentType>();
            component->deserialize(json);                       // 先反序列化（记录 pending 状态等）
            game_object.adopt_component(std::move(component));  // 再入池 + on_attach
            return true;
        }

    private:
        // 重复组件条目告警（实现在 .cpp，公开头文件不可依赖 src/Log 日志宏）
        static void report_duplicate_component(const std::string& game_object_name);
        // Meyers Singleton：函数内静态变量在首次调用时构造，
        // 避免跨翻译单元静态初始化顺序问题（s_registry 被 ID_REGISTER_COMPONENT 在全局构造期访问）
        static std::unordered_map<std::string, ComponentCreator>& get_registry();
    };
} // namespace ID

// 用于注册组件的宏
#define ID_REGISTER_COMPONENT(ComponentType, TypeName)              \
    static bool _registered_##ComponentType =                       \
        ID::ComponentFactory::register_component(TypeName,          \
            &ID::ComponentFactory::default_creator<ComponentType>)
