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

    bool ComponentFactory::create(const std::string& name, GameObject& game_object, const Json& json)
    {
        auto& registry = get_registry();
        auto it = registry.find(name);
        if (it != registry.end())
        {
            return it->second(game_object, json);
        }
        else
        {
            ID_WARN("ComponentFactory::create：尝试创建未注册的组件，名称: {}", name);
            return false;
        }
    }

    void ComponentFactory::report_duplicate_component(const std::string& game_object_name)
    {
        ID_WARN("ComponentFactory：GameObject '{}' 的存档中存在重复组件类型，跳过后续条目（池化后单实例）",
                game_object_name);
    }

    bool ComponentFactory::is_registered(const std::string& name)
    {
        auto& registry = get_registry();
        return registry.find(name) != registry.end();
    }
} // namespace ID