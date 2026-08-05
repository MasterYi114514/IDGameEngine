#pragma once

#include "IDpch.hpp"
#include "Renderer/Material/Material.hpp"

namespace ID
{
    /**
     *  实例材质（MaterialInstance），内部有父级材质（Material）的指针，允许对父级材质的参数进行局部覆盖
     * 
     */
    class ID_API MaterialInstance
    {
    public:
        MaterialInstance() = delete;
        explicit MaterialInstance(const Material* parent) : m_parent(parent) { }
        explicit MaterialInstance(const Material& parent) : m_parent(&parent) { }

        // 允许拷贝 / 移动，拷贝时是浅拷贝，享有公共的父级材质指针
        MaterialInstance(const MaterialInstance&) = default;
        MaterialInstance& operator=(const MaterialInstance&) = default;
        MaterialInstance(MaterialInstance&&) noexcept = default;
        MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

    public:
        bool is_valid() const { return m_parent != nullptr; }
        
        template<typename T>
        void set_param(const std::string& name, const T& value)
        {
            m_overrides[name] = MaterialParam(value);
        }

        void set_texture(const std::string& name, TextureID texture, uint32_t slot = 0);
        void set_sampler(const std::string& name, uint32_t slot);

        void clear_override(const std::string& name) 
            { m_overrides.erase(name); m_texture_overrides.erase(name); }
        void clear_all_overrides() 
            { m_overrides.clear(); m_texture_overrides.clear(); }

        const Material* get_parent() const { return m_parent; }
        ShaderID get_shader() const { return m_parent ? m_parent->get_shader() : ShaderID::invalid_id(); }
        bool     is_transparent() const { return m_parent && m_parent->is_transparent(); }

        // 合并父级默认值 + 覆盖，写入 shader
        void apply() const;

    private:
        const Material* m_parent = nullptr;
        std::map<std::string, MaterialParam>        m_overrides;
        std::map<std::string, TextureBindingDesc>   m_texture_overrides;
    };

    // 默认的 MaterialInstance，指向 nullptr，表示无效实例材质
    inline MaterialInstance default_material_instance{nullptr};
} // namespace ID