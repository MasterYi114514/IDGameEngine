#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"

namespace ID
{
    class ID_API ComponentFactory
    {
    public:
        using ComponentCreator = std::function<std::unique_ptr<Component>()>;

        static bool register_component(const std::string& name, ComponentCreator creator);

        static std::unique_ptr<Component> create(const std::string& name);
        static bool is_registered(const std::string& name);
    private:
        // Meyers Singleton：函数内静态变量在首次调用时构造，
        // 避免跨翻译单元静态初始化顺序问题（s_registry 被 ID_REGISTER_COMPONENT 在全局构造期访问）
        static std::unordered_map<std::string, ComponentCreator>& get_registry();
    };
} // namespace ID

// 用于注册组件的宏
#define ID_REGISTER_COMPONENT(ComponentType, TypeName)              \
    static bool _registered_##ComponentType =                       \
        ID::ComponentFactory::register_component(TypeName,          \
            []()-> std::unique_ptr<ID::Component>                   \
            {                                                       \
                return std::make_unique<ComponentType>();           \
            })
