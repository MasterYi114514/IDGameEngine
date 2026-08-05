#pragma once

#ifdef _ID_USE_IMPL

#include "Layer/Layer.hpp"
#include "Layer/LayerStack.hpp"

#include "Scene/Component/Component.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include "Log/Log.hpp"

#include <chrono>

// 简单的测试断言：条件为 false 时输出错误日志并标记失败
#define SCENE_TEST_ASSERT(cond, ...)                                          \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            ID_ERROR("[TEST FAILED] " __VA_ARGS__);                            \
            m_test_failed = true;                                              \
            return;                                                           \
        }                                                                     \
    } while (0)

namespace ID
{
    // =====================================================================
    //  测试用 Component 子类（必须先于 SceneLayer 声明，因为内联成员函数
    //  在类定义处解析，引用不到后面定义的类型）
    // =====================================================================
    class TestComponentA : public Component
    {
    public:
        int value = 0;
        TestComponentA(int v = 0) : value(v) {}

        Component::TypeID get_type_id() const override
        {
            return get_static_type_id<TestComponentA>();
        }
    };

    class TestComponentB : public Component
    {
    public:
        std::string name;
        TestComponentB(const std::string& n = "") : name(n) {}

        Component::TypeID get_type_id() const override
        {
            return get_static_type_id<TestComponentB>();
        }
    };

    // =====================================================================
    //  SceneLayer — 场景系统的压力测试与性能基准
    //      功能测试: 创建销毁 / 槽位复用 / 深层层级 / 组件索引 / 场景切换
    //      性能基准: 世界矩阵缓存 / 组件索引查找 / 创建销毁吞吐
    // =====================================================================
    class SceneLayer : public Layer
    {
    public:
        SceneLayer() : Layer("SceneLayer") {}

        void on_attach() override
        {
            m_scene = &SceneManager::create_scene("StressTestScene");
            SceneManager::load_scene(*m_scene);

            run_all_tests();
        }

        void on_detach() override
        {
            if (m_scene)
            {
                SceneManager::destroy_scene(*m_scene);
                m_scene = nullptr;
            }
        }

        void on_update(Timestep ts) override { SceneManager::on_update(ts); }
        void on_event(Event& event) override { SceneManager::on_event(event); }

    private:
        using Clock = std::chrono::high_resolution_clock;

        Scene* m_scene = nullptr;
        bool m_test_failed = false;

        // 计时辅助：从 start 到现在的毫秒数
        static double elapsed_ms(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // =================================================================
        //  测试入口
        // =================================================================
        void run_all_tests()
        {
#ifdef _ID_DEBUG
            ID_WARN("当前为 Debug 构建，性能数据仅供参考（Release 下才有意义）");
#endif
            ID_INFO("========== Scene 压力测试开始 ==========");
            m_test_failed = false;

            // ---- 功能测试（验证正确性 + 记录耗时）----
            test_create_and_destroy();
            test_slot_reuse();
            test_deep_hierarchy(50);
            test_component_operations();
            test_bulk_create_destroy(1000);
            test_scene_switch();

            // ---- 性能基准（只报告数据，不判定成败）----
            benchmark_world_matrix(50, 100000);
            benchmark_component_lookup(1000000);
            benchmark_create_destroy(10000, 10);

            if (m_test_failed)
                ID_ERROR("========== Scene 压力测试存在失败项 ==========");
            else
                ID_INFO("========== Scene 压力测试全部通过 ==========");
        }

        // =================================================================
        //  测试 1：创建与销毁
        // =================================================================
        void test_create_and_destroy()
        {
            ID_INFO("[Test 1] 基本创建与销毁...");
            auto start = Clock::now();

            auto id1 = m_scene->create_game_object("TestObj1");
            auto id2 = m_scene->create_game_object("TestObj2");
            SCENE_TEST_ASSERT(id1 != id2, "两个 GameObject 应该有不同 ID");

            size_t count_before = m_scene->get_game_object_count();
            SCENE_TEST_ASSERT(count_before == 2, "应该有 2 个 GameObject，当前: {}", count_before);

            m_scene->destroy_game_object(id1);
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == 1, "销毁后应有 1 个 GameObject");

            m_scene->destroy_game_object(id2);
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == 0, "全部销毁后应有 0 个 GameObject");

            ID_INFO("[Test 1] 通过 ✅ ({:.2f} ms)", elapsed_ms(start));
        }

        // =================================================================
        //  测试 2：槽位复用
        // =================================================================
        void test_slot_reuse()
        {
            ID_INFO("[Test 2] 槽位复用...");
            auto start = Clock::now();

            auto id1 = m_scene->create_game_object("A");
            m_scene->destroy_game_object(id1);
            auto id2 = m_scene->create_game_object("B");

            // 销毁 id1 后立即创建的新对象应复用 id1 的槽位
            SCENE_TEST_ASSERT(id1 == id2, "销毁后新建的 GameObject ID 应复用旧槽位，id1={}, id2={}", id1, id2);

            m_scene->destroy_game_object(id2);
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == 0, "清理完毕");

            ID_INFO("[Test 2] 通过 ✅ ({:.2f} ms)", elapsed_ms(start));
        }

        // =================================================================
        //  测试 3：深层父子层级 + 世界矩阵
        // =================================================================
        void test_deep_hierarchy(int depth)
        {
            ID_INFO("[Test 3] 深层层级 (深度={})...", depth);
            auto start = Clock::now();

            // 构建一条深度为 depth 的单链
            std::vector<GameObject::ID> chain;
            chain.reserve(depth);

            GameObject::ID root = m_scene->create_game_object("Root");
            chain.push_back(root);

            for (int i = 1; i < depth; i++)
            {
                auto child = m_scene->create_game_object("Node_" + std::to_string(i));
                m_scene->get_game_object(child).set_parent(chain.back());
                chain.push_back(child);
            }

            // --- 3a: 测试 get_world_matrix() 的缓存（连续调用应命中缓存）---
            const auto& leaf = m_scene->get_game_object(chain.back());
            Mat4 mat1 = leaf.get_world_matrix();
            Mat4 mat2 = leaf.get_world_matrix();
            SCENE_TEST_ASSERT(memcmp(mat1.get_data(), mat2.get_data(), sizeof(float) * 16) == 0,
                      "连续两次 get_world_matrix() 结果应相同（缓存命中）");

            // --- 3b: 修改中间节点的 transform，验证脏标记向下传播 ---
            auto& mid_node = m_scene->get_game_object(chain[depth / 2]);
            mid_node.get_transform().set_position(Pos3(10.0f, 0.0f, 0.0f));

            Mat4 mat_after_move = leaf.get_world_matrix();
            // 平移 10 个单位后，世界矩阵应有变化
            bool changed = memcmp(mat1.get_data(), mat_after_move.get_data(), sizeof(float) * 16) != 0;
            SCENE_TEST_ASSERT(changed, "中间节点位移后，叶节点世界矩阵应发生变化");

            // --- 3c: 修改叶节点自身，验证本地 dirty 也正确传播到世界 ---
            auto& leaf_mut = m_scene->get_game_object(chain.back());
            leaf_mut.get_transform().set_scale(Vec3(2.0f, 2.0f, 2.0f));
            Mat4 mat_after_scale = leaf.get_world_matrix();
            bool scaled = memcmp(mat_after_move.get_data(), mat_after_scale.get_data(), sizeof(float) * 16) != 0;
            SCENE_TEST_ASSERT(scaled, "叶节点缩放后，世界矩阵应发生变化");

            // --- 清理 ---
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                m_scene->destroy_game_object(*it);
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == 0, "清理后应为 0");

            ID_INFO("[Test 3] 通过 ✅ ({:.2f} ms)", elapsed_ms(start));
        }

        // =================================================================
        //  测试 4：Component 操作 + 索引表
        // =================================================================
        void test_component_operations()
        {
            ID_INFO("[Test 4] Component 操作...");
            auto start = Clock::now();

            auto go_id = m_scene->create_game_object("ComponentHost");
            auto& go = m_scene->get_game_object(go_id);

            // --- 4a: has_component ---
            SCENE_TEST_ASSERT(!go.has_component<TestComponentA>(),
                      "初始不应有 TestComponentA");

            // --- 4b: add_component + get_component (O(1) 索引) ---
            auto& comp_a = go.add_component<TestComponentA>(42);
            SCENE_TEST_ASSERT(go.has_component<TestComponentA>(),
                      "添加后应有 TestComponentA");

            TestComponentA* found = go.get_component<TestComponentA>();
            SCENE_TEST_ASSERT(found != nullptr, "get_component 应找到 TestComponentA");
            SCENE_TEST_ASSERT(found->value == 42, "返回的组件值应为 42, 当前: {}", found->value);

            // --- 4c: 同类型多个组件（索引指向第一个）---
            go.add_component<TestComponentA>(99);
            TestComponentA* first = go.get_component<TestComponentA>();
            SCENE_TEST_ASSERT(first != nullptr && first->value == 42,
                      "get_component 应始终返回第一个同类型组件（索引不因新增而变）");

            // --- 4d: remove_component（索引维护）---
            go.remove_component<TestComponentA>();  // 删除 value=42 的那个（索引指向的）
            TestComponentA* second = go.get_component<TestComponentA>();
            SCENE_TEST_ASSERT(second != nullptr && second->value == 99,
                      "删除索引指向的组件后，索引应自动更新到下一个");

            go.remove_component<TestComponentA>();  // 删除最后一个
            SCENE_TEST_ASSERT(!go.has_component<TestComponentA>(),
                      "删除全部后不应再有 TestComponentA");

            // --- 4e: 多类型组件混搭 ---
            go.add_component<TestComponentA>(1);
            go.add_component<TestComponentB>("hello");
            SCENE_TEST_ASSERT(go.has_component<TestComponentA>() && go.has_component<TestComponentB>(),
                      "应同时拥有两种组件");
            TestComponentB* comp_b = go.get_component<TestComponentB>();
            SCENE_TEST_ASSERT(comp_b != nullptr && comp_b->name == "hello",
                      "TestComponentB 的值应正确");

            m_scene->destroy_game_object(go_id);
            ID_INFO("[Test 4] 通过 ✅ ({:.2f} ms)", elapsed_ms(start));
        }

        // =================================================================
        //  测试 5：批量创建与销毁（压力）
        // =================================================================
        void test_bulk_create_destroy(int count)
        {
            ID_INFO("[Test 5] 批量创建销毁 (N={})...", count);
            auto start = Clock::now();

            // 记录初始数量，不假设场景为空（防止前序测试残留影响）
            size_t base_count = m_scene->get_game_object_count();

            std::vector<GameObject::ID> ids;
            ids.reserve(count);

            // 批量创建
            for (int i = 0; i < count; i++)
            {
                auto id = m_scene->create_game_object("Bulk_" + std::to_string(i));
                ids.push_back(id);
            }
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == base_count + static_cast<size_t>(count),
                      "批量创建后数量应为 {}, 当前: {}",
                      base_count + count, m_scene->get_game_object_count());

            // 奇数索引的销毁（制造空洞），并记录被销毁的 ID
            std::unordered_set<GameObject::ID> destroyed_ids;
            for (int i = 1; i < count; i += 2)
            {
                m_scene->destroy_game_object(ids[i]);
                destroyed_ids.insert(ids[i]);
            }
            size_t expected_remaining = base_count + static_cast<size_t>((count + 1) / 2);  // base + ceil(count/2)
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == expected_remaining,
                      "隔位销毁后剩余 {}, 当前: {}",
                      expected_remaining, m_scene->get_game_object_count());

            // 重新创建（应复用空洞）。注意：m_freed_ids 是 unordered_set，
            // begin() 顺序任意，且可能残留前序测试的空洞，因此不假设具体 ID。
            // 只验证：refill 的 ID 都在已分配范围内（<= max_id），即没有扩容。
            GameObject::ID max_id = 0;
            for (auto id : ids) max_id = std::max(max_id, id);

            std::vector<GameObject::ID> refill_ids;
            refill_ids.reserve(count / 2);
            for (int i = 0; i < count / 2; i++)
            {
                refill_ids.push_back(m_scene->create_game_object("Refill_" + std::to_string(i)));
            }
            for (auto id : refill_ids)
            {
                SCENE_TEST_ASSERT(id <= max_id,
                          "refill ID {} 应复用旧槽位（<= max_id={}），不得扩容", id, max_id);
            }

            // 清理：先销毁 refill（占用被销毁的槽位），再销毁存活的旧对象（偶数索引）
            for (auto id : refill_ids)
                m_scene->destroy_game_object(id);
            for (int i = 0; i < count; i += 2)
                m_scene->destroy_game_object(ids[i]);
            SCENE_TEST_ASSERT(m_scene->get_game_object_count() == base_count,
                      "清理后应回到初始数量 {}, 当前: {}", base_count, m_scene->get_game_object_count());

            ID_INFO("[Test 5] 通过 ✅ ({:.2f} ms)", elapsed_ms(start));
        }

        // =================================================================
        //  测试 6：场景切换
        // =================================================================
        void test_scene_switch()
        {
            ID_INFO("[Test 6] 场景切换...");
            auto start = Clock::now();

            // 创建第二个场景
            auto& scene2 = SceneManager::create_scene("SwitchScene");

            // 当前场景应仍为 StressTestScene
            SCENE_TEST_ASSERT(&SceneManager::get_current_scene() == m_scene,
                      "当前场景应为 StressTestScene");

            // 切换到新场景
            SceneManager::load_scene(scene2);
            SCENE_TEST_ASSERT(&SceneManager::get_current_scene() == &scene2,
                      "load_scene 后当前场景应切换");

            SCENE_TEST_ASSERT(!m_scene->get_is_running(),
                      "旧场景应已暂停");

            SCENE_TEST_ASSERT(scene2.get_is_running(),
                      "新场景应正在运行");

            // 切回来
            SceneManager::load_scene(*m_scene);
            SCENE_TEST_ASSERT(&SceneManager::get_current_scene() == m_scene,
                      "切回 StressTestScene");

            // 清理
            SceneManager::destroy_scene(scene2);

            ID_INFO("[Test 6] 通过 ✅ ({:.2f} ms)", elapsed_ms(start));
        }

        // =================================================================
        //  基准 1：get_world_matrix() 性能（缓存命中 vs 脏重建）
        //      深度 depth 的单链，iterations 次调用
        // =================================================================
        void benchmark_world_matrix(int depth, int iterations)
        {
            ID_INFO("[Bench 1] get_world_matrix() 性能 (深度={}, 迭代={})...", depth, iterations);

            // 构建深层级
            std::vector<GameObject::ID> chain;
            chain.reserve(depth);
            GameObject::ID root = m_scene->create_game_object("BenchRoot");
            chain.push_back(root);
            for (int i = 1; i < depth; i++)
            {
                auto child = m_scene->create_game_object("BenchNode_" + std::to_string(i));
                m_scene->get_game_object(child).set_parent(chain.back());
                chain.push_back(child);
            }
            auto& leaf = m_scene->get_game_object(chain.back());

            // --- 场景 A：连续调用（缓存命中，无 dirty）---
            leaf.get_world_matrix();    // 预热：先计算一次
            volatile float sink = 0.0f;
            auto start = Clock::now();
            for (int i = 0; i < iterations; i++)
            {
                const Mat4& m = leaf.get_world_matrix();
                sink += m.get_data()[0];
            }
            double cached_ms = elapsed_ms(start);

            // --- 场景 B：每次修改后调用（触发 dirty 传播 + 重建）---
            start = Clock::now();
            for (int i = 0; i < iterations; i++)
            {
                leaf.get_transform().translate(Vec3(0.001f, 0.0f, 0.0f));  // 使自身及子链变脏
                const Mat4& m = leaf.get_world_matrix();
                sink += m.get_data()[0];
            }
            double dirty_ms = elapsed_ms(start);

            ID_INFO("[Bench 1] 缓存命中: {:>9.3f} ms ({:>6.1f} ns/次)  |  脏重建: {:>9.3f} ms ({:>6.1f} ns/次)",
                cached_ms, cached_ms / iterations * 1e6,
                dirty_ms,  dirty_ms  / iterations * 1e6);
            (void)sink;

            // 清理
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                m_scene->destroy_game_object(*it);
        }

        // =================================================================
        //  基准 2：get_component() 索引查找性能
        // =================================================================
        void benchmark_component_lookup(int iterations)
        {
            ID_INFO("[Bench 2] get_component() 索引查找 (迭代={})...", iterations);

            auto go_id = m_scene->create_game_object("BenchComponent");
            auto& go = m_scene->get_game_object(go_id);
            go.add_component<TestComponentA>(1);
            go.add_component<TestComponentB>("bench");

            volatile uint32_t sink = 0;
            go.get_component<TestComponentA>();   // 预热
            auto start = Clock::now();
            for (int i = 0; i < iterations; i++)
            {
                TestComponentA* c = go.get_component<TestComponentA>();
                if (c) sink += static_cast<uint32_t>(c->value);
            }
            double lookup_ms = elapsed_ms(start);

            ID_INFO("[Bench 2] 索引查找: {:>9.3f} ms ({:>6.1f} ns/次)", lookup_ms, lookup_ms / iterations * 1e6);
            (void)sink;

            m_scene->destroy_game_object(go_id);
        }

        // =================================================================
        //  基准 3：创建 + 销毁吞吐
        // =================================================================
        void benchmark_create_destroy(int count, int rounds)
        {
            ID_INFO("[Bench 3] 创建+销毁吞吐 (N={}, 轮数={})...", count, rounds);

            auto start = Clock::now();
            size_t total_ops = 0;
            for (int r = 0; r < rounds; r++)
            {
                std::vector<GameObject::ID> ids;
                ids.reserve(count);
                for (int i = 0; i < count; i++)
                    ids.push_back(m_scene->create_game_object("Bench_" + std::to_string(i)));
                for (auto id : ids)
                    m_scene->destroy_game_object(id);
                total_ops += static_cast<size_t>(count) * 2;  // 创建 + 销毁
            }
            double total_ms = elapsed_ms(start);
            double per_op_us = total_ms / total_ops * 1000.0;

            ID_INFO("[Bench 3] 总耗时: {:>9.3f} ms  |  平均 {:.2f} µs/次(创建或销毁)",
                total_ms, per_op_us);
        }
    };

} // namespace ID

#endif
