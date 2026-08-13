#pragma once

#include "ID.hpp"
#include <Log/Log.hpp>

using namespace ID;

class SceneLayer : public Layer
{
public:
    SceneLayer() : Layer("SceneLayer") { }

    void on_attach() override
    {
        ID_TRACE("[SceneLayer] on_attach 开始");
        m_scene = &SceneManager::create_scene("SandboxScene");
        SceneManager::load_scene(*m_scene);
        ID_TRACE("[SceneLayer] Scene 创建并加载完成");

        // ── Man：头用球体，四肢和身体用长方体（碰撞体中心为原点，总高约 1.95）──
        ShaderID man_shader = ID::ShaderManager::create(
            std::string(ASSETS_PATH) + "/shader/geometry.vsl",
            std::string(ASSETS_PATH) + "/shader/geometry.fsl");
        TextureID man_texture = ID::TextureManager::load_texture(std::string(ASSETS_PATH) + "/texture/1.png");

        Material* man_material = MaterialLibrary::add(man_shader, "ManMaterial");
        man_material->set_param("u_color", Vec3(0.8f, 0.8f, 0.8f));
        man_material->set_texture("texture_sampler", man_texture, 0);

        // 各部位材质实例：头肤色，身体/手臂蓝色，腿深色
        MaterialInstance skin_mat(*man_material);
        skin_mat.set_param("u_color", Vec3(0.85f, 0.70f, 0.60f));
        MaterialInstance cloth_mat(*man_material);
        cloth_mat.set_param("u_color", Vec3(0.25f, 0.45f, 0.85f));
        MaterialInstance dark_mat(*man_material);
        dark_mat.set_param("u_color", Vec3(0.20f, 0.25f, 0.35f));

        // 共享网格：头球体，身体/手臂/腿长方体
        MeshID head_mesh = MeshFactory::create_sphere(0.2f);
        MeshID body_mesh = MeshFactory::create_cuboid(0.7f, 0.7f, 0.35f);
        MeshID arm_mesh  = MeshFactory::create_cuboid(0.18f, 0.6f, 0.18f);
        MeshID leg_mesh  = MeshFactory::create_cuboid(0.24f, 0.85f, 0.24f);

        GameObject::ID man = m_scene->create_game_object("Man");
        m_man = &(m_scene->get_game_object(man));
        // 碰撞体中心 = 身体中心（y≈0.975），初始位置抬高使脚底恰好贴地
        m_man->add_component<TransformComponent>().set_position(Vec3(0.0f, 0.975f, 0.0f));

        GameObject::ID man_head = m_scene->create_game_object("head");
        GameObject::ID man_body = m_scene->create_game_object("body");
        GameObject::ID man_left_arm = m_scene->create_game_object("left_arm");
        GameObject::ID man_right_arm = m_scene->create_game_object("right_arm");
        GameObject::ID man_left_leg = m_scene->create_game_object("left_leg");
        GameObject::ID man_right_leg = m_scene->create_game_object("right_leg");

        m_scene->get_game_object(man_head).set_parent(man);
        m_scene->get_game_object(man_body).set_parent(man);
        m_scene->get_game_object(man_left_arm).set_parent(man);
        m_scene->get_game_object(man_right_arm).set_parent(man);
        m_scene->get_game_object(man_left_leg).set_parent(man);
        m_scene->get_game_object(man_right_leg).set_parent(man);

        // 统一给各部位设置位置 + 挂载 TransformComponent + MeshRendererComponent
        auto add_part = [this](GameObject::ID id, const Pos3& pos, MeshID mesh, const MaterialInstance& material)
        {
            GameObject& go = m_scene->get_game_object(id);
            go.add_component<TransformComponent>().set_position(pos);
            go.add_component<MeshRendererComponent>(Model(mesh, material));
        };

        // 各部位位置为相对 Man 的局部坐标（相对身体中心，世界位置 = Man 位置 + 局部位置）
        add_part(man_head,      Pos3(0.0f,    0.775f, 0.0f), head_mesh, skin_mat);   // 头：球体
        add_part(man_body,      Pos3(0.0f,    0.225f, 0.0f), body_mesh, cloth_mat);  // 身体：长方体
        add_part(man_left_arm,  Pos3(-0.47f,  0.275f, 0.0f), arm_mesh,  cloth_mat);  // 左臂
        add_part(man_right_arm, Pos3(0.47f,   0.275f, 0.0f), arm_mesh,  cloth_mat);  // 右臂
        add_part(man_left_leg,  Pos3(-0.15f, -0.55f,  0.0f), leg_mesh, dark_mat);    // 左腿
        add_part(man_right_leg, Pos3(0.15f,  -0.55f,  0.0f), leg_mesh, dark_mat);    // 右腿

        // Man 整体：动态刚体，用胶囊近似全身（半径 0.35、圆柱高 1.25，总高约 1.95，中心在身体中心）
        RigidBodyComponent& man_rb = m_man->add_component<RigidBodyComponent>();
        man_rb.set_type(RigidBodyType::Dynamic);
        man_rb.set_collider_shape(ColliderShape::make_capsule(0.35f, 1.25f));
        ID_TRACE("[SceneLayer] Man 刚体添加完成");

        // 创建地面
        GameObject::ID ground = m_scene->create_game_object("Ground");
        m_scene->get_game_object(ground).add_component<MeshRendererComponent>(
            Model(MeshFactory::create_cuboid(20.0f, 0.1f, 20.0f), skin_mat)
        );
        m_scene->get_game_object(ground).add_component<TransformComponent>().set_position(Vec3(0.0f, 0.0f, 0.0f));

        // 地面：静态刚体（Box 碰撞体与视觉 cuboid 对齐，顶面 y=0.05）
        RigidBodyComponent& ground_rb = m_scene->get_game_object(ground).add_component<RigidBodyComponent>();
        ground_rb.set_type(RigidBodyType::Static);
        ground_rb.set_mass(0.0f);
        ground_rb.set_collider_shape(ColliderShape::make_box(Vec3(10.0f, 0.05f, 10.0f)));
        // 给地面设非零 restitution，Bullet 乘法组合：地面 0.5 × 球 0.8 = 0.4，球会弹
        PhysicsMaterial ground_mat;
        ground_mat.restitution = 0.5f;
        ground_mat.friction     = 0.6f;
        ground_rb.set_material(ground_mat);
        ID_TRACE("[SceneLayer] 地面创建完成（含静态刚体）");

        // ── 小球：物理行为测试 ──
        MeshID ball_mesh = MeshFactory::create_sphere(0.2f);
        MaterialInstance red_mat(*man_material);     red_mat.set_param("u_color", Vec3(0.90f, 0.25f, 0.25f));
        MaterialInstance green_mat(*man_material);   green_mat.set_param("u_color", Vec3(0.25f, 0.85f, 0.35f));
        MaterialInstance blue_mat(*man_material);    blue_mat.set_param("u_color", Vec3(0.25f, 0.45f, 0.95f));
        MaterialInstance yellow_mat(*man_material);  yellow_mat.set_param("u_color", Vec3(0.95f, 0.80f, 0.20f));

        // 创建一个小球：动态刚体 + 球形碰撞体，返回其 RigidBodyComponent 便于后续施加速度
        auto add_ball = [this, ball_mesh](const Vec3& pos, const MaterialInstance& material, float restitution) -> RigidBodyComponent*
        {
            GameObject::ID ball = m_scene->create_game_object("Ball");
            GameObject& go = m_scene->get_game_object(ball);
            go.add_component<TransformComponent>().set_position(pos);
            go.add_component<MeshRendererComponent>(Model(ball_mesh, material));

            RigidBodyComponent& rb = go.add_component<RigidBodyComponent>();
            rb.set_type(RigidBodyType::Dynamic);
            rb.set_collider_shape(ColliderShape::make_sphere(0.2f));
            PhysicsMaterial mat;
            mat.restitution = restitution;
            rb.set_material(mat);
            return &rb;
        };

        // 红色：普通下落
        add_ball(Vec3(-3.0f, 3.0f, 0.0f), red_mat, 0.0f);
        // 绿色：从更高处下落
        add_ball(Vec3(-1.0f, 5.0f, 0.0f), green_mat, 0.0f);
        // 蓝色：高弹性（restitution 0.8），落地弹跳
        add_ball(Vec3(1.0f, 4.0f, 0.0f), blue_mat, 0.8f);
        // 黄色：首帧施加水平初速度，展示滚动
        // m_rolling_ball = add_ball(Vec3(3.0f, 6.0f, 0.0f), yellow_mat, 0.0f);
        ID_TRACE("[SceneLayer] 四个小球创建完成");

        // 设置方向光（模拟太阳）
        GameObject::ID light = m_scene->create_game_object("Light");
        m_light = &(m_scene->get_game_object(light));

        Light sun_light;
        sun_light.type      = LightType::Directional;
        sun_light.drop.direction = Vec3(0.5f, -0.8f, -0.3f);  // 斜向下
        sun_light.color     = Vec3(1.0f, 0.95f, 0.85f);        // 暖白色
        sun_light.intensity = 1.2f;
        m_light->add_component<LightComponent>(sun_light);
        ID_TRACE("[SceneLayer] on_attach 完成");
    }

    void on_update(Timestep ts) override
    {
        // 驱动物理模拟（PhysicsSystem 内部会同步刚体位姿）
        SceneManager::on_update(ts);
        // update_man(ts);

        // 首帧给黄色小球一个水平初速度，展示滚动效果（刚体已在本次 on_update 中创建）
        if (!m_initial_velocity_applied)
        {
            m_initial_velocity_applied = true;
            if (m_rolling_ball)
                m_rolling_ball->set_linear_velocity(Vec3(4.0f, 0.0f, 0.0f));
        }
    }

    void on_event(Event& event) override
    {
        // SceneManager::on_event(event);
    }

public:
    Scene* get_scene() const { return m_scene; }

private:
    void update_man(Timestep ts)
    {
        if(Input::is_key_pressed(KeyCodes::W))
        {
            m_man->get_component<TransformComponent>()->translate(Vec3(0.0f, 0.0f, -1.0f) * ts.get_seconds());
        }
        if(Input::is_key_pressed(KeyCodes::S))
        {
            m_man->get_component<TransformComponent>()->translate(Vec3(0.0f, 0.0f, 1.0f) * ts.get_seconds());
        }
        if(Input::is_key_pressed(KeyCodes::A))
        {
            m_man->get_component<TransformComponent>()->translate(Vec3(-1.0f, 0.0f, 0.0f) * ts.get_seconds());
        }
        if(Input::is_key_pressed(KeyCodes::D))
        {
            m_man->get_component<TransformComponent>()->translate(Vec3(1.0f, 0.0f, 0.0f) * ts.get_seconds());
        }
    }

private:
    Scene* m_scene = nullptr;
    GameObject* m_man = nullptr;
    GameObject* m_light = nullptr;
    RigidBodyComponent* m_rolling_ball = nullptr;   // 黄色滚动测试球
    bool m_initial_velocity_applied = false;        // 首帧初速度是否已施加
};