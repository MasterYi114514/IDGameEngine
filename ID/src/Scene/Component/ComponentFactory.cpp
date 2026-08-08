#include "Scene/Component/ComponentFactory.hpp"
#include "Log/Log.hpp"

namespace ID
{
    std::unordered_map<std::string, ComponentFactory::ComponentCreator>& ComponentFactory::get_registry()
    {
        // 函数内静态变量：首次调用时构造，确保先于所有 ID_REGISTER_COMPONENT 注册使用
        static std::unordered_map<std::string, ComponentCreator> s_registry;
        return s_registry;
    }

    bool ComponentFactory::register_component(const std::string& name, ComponentCreator creator)
    {
        auto& registry = get_registry();
        if (registry.find(name) != registry.end())
        {
            ID_WARN("ComponentFactory::register_component：尝试重复注册名为 {} 的组件", name);
            return false;
        }

        registry[name] = std::move(creator);
        return true;
    }

    std::unique_ptr<Component> ComponentFactory::create(const std::string& name)
    {
        auto& registry = get_registry();
        auto it = registry.find(name);
        if (it != registry.end())
        {
            return it->second();
        }
        else
        {
            ID_WARN("ComponentFactory::create：尝试创建未注册的组件，名称: {}", name);
            return nullptr;
        }
    }

    bool ComponentFactory::is_registered(const std::string& name)
    {
        auto& registry = get_registry();
        return registry.find(name) != registry.end();
    }
} // namespace ID