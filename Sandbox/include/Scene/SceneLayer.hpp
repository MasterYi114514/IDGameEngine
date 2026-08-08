#pragma once

#include "ID.hpp"

using namespace ID;

class SceneLayer : public Layer
{
public:
    SceneLayer() : Layer("SceneLayer") { }

    void on_attach() override
    {
        m_scene = &SceneManager::create_scene("SandboxScene");
        SceneManager::load_scene(*m_scene);

        // ── Man：头用球体，四肢和身体用长方体（脚底为原点，总高约 1.95）──
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

        // 统一给各部位设置位置 + 挂载 MeshRendererComponent
        auto add_part = [this](GameObject::ID id, const Pos3& pos, MeshID mesh, const MaterialInstance& material)
        {
            GameObject& go = m_scene->get_game_object(id);
            go.get_transform().set_position(pos);
            go.add_component<MeshRendererComponent>(Model(mesh, material));
        };

        add_part(man_head,      Pos3(0.0f,    1.75f, 0.0f), head_mesh, skin_mat);    // 头：球体
        add_part(man_body,      Pos3(0.0f,    1.20f, 0.0f), body_mesh, cloth_mat);   // 身体：长方体
        add_part(man_left_arm,  Pos3(-0.47f,  1.25f, 0.0f), arm_mesh,  cloth_mat);   // 左臂
        add_part(man_right_arm, Pos3(0.47f,   1.25f, 0.0f), arm_mesh,  cloth_mat);   // 右臂
        add_part(man_left_leg,  Pos3(-0.15f,  0.425f, 0.0f), leg_mesh, dark_mat);    // 左腿
        add_part(man_right_leg, Pos3(0.15f,   0.425f, 0.0f), leg_mesh, dark_mat);    // 右腿

        // 创建地面
        GameObject::ID ground = m_scene->create_game_object("Ground");
        m_scene->get_game_object(ground).add_component<MeshRendererComponent>(
            Model(MeshFactory::create_cuboid(20.0f, 0.1f, 20.0f), skin_mat)
        );

        // 设置方向光（模拟太阳）
        GameObject::ID light = m_scene->create_game_object("Light");
        m_light = &(m_scene->get_game_object(light));

        Light sun_light;
        sun_light.type      = LightType::Directional;
        sun_light.drop.direction = Vec3(0.5f, -0.8f, -0.3f);  // 斜向下
        sun_light.color     = Vec3(1.0f, 0.95f, 0.85f);        // 暖白色
        sun_light.intensity = 1.2f;
        m_light->add_component<LightComponent>(sun_light);
    }

    void on_update(Timestep ts) override
    {
        // SceneManager::on_update(ts);
        // update_man(ts);
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
            m_man->get_transform().translate(Vec3(0.0f, 0.0f, -1.0f) * ts.get_seconds());
        }
        if(Input::is_key_pressed(KeyCodes::S))
        {
            m_man->get_transform().translate(Vec3(0.0f, 0.0f, 1.0f) * ts.get_seconds());
        }
        if(Input::is_key_pressed(KeyCodes::A))
        {
            m_man->get_transform().translate(Vec3(-1.0f, 0.0f, 0.0f) * ts.get_seconds());
        }
        if(Input::is_key_pressed(KeyCodes::D))
        {
            m_man->get_transform().translate(Vec3(1.0f, 0.0f, 0.0f) * ts.get_seconds());
        }
    }

private:
    Scene* m_scene = nullptr;
    GameObject* m_man = nullptr;
    GameObject* m_light = nullptr;
};