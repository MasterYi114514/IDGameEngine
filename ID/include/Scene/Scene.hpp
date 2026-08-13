#pragma once

#include "IDpch.hpp"
#include "Scene/SceneID.hpp"
#include "Scene/Component/Component.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/System/PhysicsSystem.hpp"

namespace ID
{
    class ID_API Scene : public SerializableBase
    {
    private:
        // 设置构造函数为 private，确保只能通过 SceneManager 创建和管理 Scene 实例
        friend class SceneManager;
        explicit Scene(const std::string& name = "Untitled Scene", SceneID id = SceneID{});
        
    public:
        ~Scene() = default;

        // 禁止拷贝
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        // 允许移动
        Scene(Scene&&) = default;
        Scene& operator=(Scene&&) = default;

    public:
        // 运行时唯一标识（SceneManager 分配，不随改名/序列化变化）
        SceneID get_id() const { return m_id; }
        /**
         *  @brief 创建一个新的 GameObject
         *  @param name GameObject 的名称
         *  @return 返回新创建的 GameObject 的引用
         *   
         *  优先复用空闲的槽位，如果没有空闲槽位，则在末尾创建一个新的 GameObject
         */
        GameObject::ID create_game_object(const std::string& name = "GameObject");

        /**
         *  @brief 销毁一个 GameObject
         *  @param id 要销毁的 GameObject 的 ID
         *   
         *  将指定 ID 的 GameObject 标记为已释放，并将其槽位加入空闲列表，以便后续复用
         */
        void destroy_game_object(GameObject::ID id);

        /**
         *  @brief 获取指定 ID 的 GameObject
         *  @param id 要获取的 GameObject 的 ID
         *  @return 返回指定 ID 的 GameObject 的引用，如果 ID 无效则抛出异常
         */
        GameObject& get_game_object(GameObject::ID id) { return *m_game_objects.at(id); }
        const GameObject& get_game_object(GameObject::ID id) const { return *m_game_objects.at(id); }

        /**
         *  @brief 查找具有指定组件的 GameObject
         *  @tparam ComponentType 要查找的组件类型
         *  @return 返回具有指定组件的 GameObject 的 ID 列表
         */
        template<typename ComponentType>
        std::vector<GameObject::ID> find_game_objects_with_component() const;

        // 获取有效的 GameObject 数量
        size_t get_game_object_count() const { return m_game_objects.size() - m_freed_ids.size(); }

        /**
         *  @brief 获取内部数组的容量（= 最大已分配 ID + 1）
         *  @return capacity，范围 [0, capacity) 内的 ID 可通过 is_game_object_valid 检查
         *
         *  配合 is_game_object_valid() 实现 O(n) 无分配遍历，
         *  替代每帧分配新 vector 的 get_valid_object_ids()。
         */
        size_t get_game_object_capacity() const { return m_game_objects.size(); }

        /**
         *  @brief 检查指定 ID 是否为有效（存活）的 GameObject
         *  @param id 目标 ID
         *  @return true = 有效，可安全调用 get_game_object()
         */
        bool is_game_object_valid(GameObject::ID id) const;

        const std::string& get_name() const { return m_name; }
        void set_name(const std::string& name) { m_name = name; }
        bool get_is_running() const { return m_is_running; }
        
        void set_running() { m_is_running = true; }
        void set_paused() { m_is_running = false; }

    public:
        void on_update(Timestep ts);
        void on_event(Event& event);

    public:
        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;

        /// 获取物理系统（只读）
        const PhysicsSystem& get_physics_system() const { return m_physics_system; }

        /// 获取物理系统（可修改，供 DevGUI 物理面板等使用）
        PhysicsSystem& get_physics_system() { return m_physics_system; }

    private:
        SceneID m_id;                       // 运行时唯一标识
        std::string m_name;
        bool m_is_running = false;
        std::vector<std::unique_ptr<GameObject>>    m_game_objects;
        std::unordered_set<GameObject::ID>          m_freed_ids;        // 已释放的 GameObject ID
        PhysicsSystem m_physics_system;
    };

    template<typename ComponentType>
    std::vector<GameObject::ID> Scene::find_game_objects_with_component() const
    {
        static_assert(std::is_base_of<Component, ComponentType>::value, "传入的类型必须是 Component 的子类");

        std::vector<GameObject::ID> result;
        for (const auto& game_object_ptr : m_game_objects)
        {
            if (game_object_ptr && game_object_ptr->has_component<ComponentType>())
            {
                result.push_back(game_object_ptr->get_id());
            }
        }
        return result;
    }
} // namespace ID