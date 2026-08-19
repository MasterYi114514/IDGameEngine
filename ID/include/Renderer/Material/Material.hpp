#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Material/MaterialParam.hpp"

namespace ID
{
    struct ID_API TextureBindingDesc : SerializableBase
    {
        TextureBindingDesc() = default;
        TextureBindingDesc(TextureID texture, uint32_t slot) : texture(texture), slot(slot) { }
        
        TextureID   texture = TextureID::invalid_id();
        uint32_t    slot = 0;

        Json serialize(ArenaID arena_id) const override;
        void deserialize(const Json& json) override;
    };

    class ID_API Material : public SerializableBase
    {
    public:
        Material() = delete;
        explicit Material(ShaderID shader, const std::string& name = "Material");
        ~Material() = default;

        // 值类型：允许拷贝 / 移动
        Material(const Material&) = default;
        Material& operator=(const Material&) = default;
        Material(Material&&) noexcept = default;
        Material& operator=(Material&&) noexcept = default;

    public:
        // MaterialParam 相关 API
        template<typename T>
        requires MPSupported<T>
        void set_param(const std::string& name, const T& value)
        {
            m_param_defaults[name] = MaterialParam(value);
        }

        bool has_param(const std::string& name) const;
        void remove_param(const std::string& name) { m_param_defaults.erase(name); }

    public:
        // TextureBindingDesc 相关 API
        void set_texture(const std::string& name, TextureID texture, uint32_t slot = 0);
        void set_sampler(const std::string& name, uint32_t slot);       // 仅写采样器槽位，TextureID 无意义
        void remove_texture(const std::string& name) { m_texture_defaults.erase(name); }

    public:
        // 混合（透明排序 / 管线混合状态）相关 API
        void set_transparent(bool transparent) { m_transparent = transparent; }
        bool is_transparent() const { return m_transparent; }

    public:
        // 基础查询接口
        ShaderID get_shader() const { return m_shader; }
        const std::string& get_name() const { return m_name; }
        void set_name(const std::string& name) { m_name = name; }
        const std::map<std::string, MaterialParam>& get_param_defaults() const { return m_param_defaults; }
        const std::map<std::string, TextureBindingDesc>& get_texture_defaults() const 
            { return m_texture_defaults; }

    public:
        /**
         *  @brief 将材质的默认参数应用到当前渲染管线 
         *  底层会调用 apply_param() 将每个参数写入管线的 shader
         */
        void apply() const;

        static void apply_param(ShaderID shader, const std::string& name, const MaterialParam& param);

    private:
        // 序列化与反序列化，只允许友元 MaterialLibrary 调用
        friend class MaterialLibrary;
        Json serialize(ArenaID arena_id) const override;
        void deserialize(const Json& json) override;

    private:
        ShaderID        m_shader;
        std::string     m_name;
        std::map<std::string, MaterialParam>        m_param_defaults;
        std::map<std::string, TextureBindingDesc>   m_texture_defaults;

        bool            m_transparent = false;      // 是否透明
    };
} // namespace ID