#pragma once

#include "IDpch.hpp"
#include "Renderer/Material/Material.hpp"

namespace ID
{
    class ID_API MaterialLibrary
    {
    public:
        MaterialLibrary() = delete;
        ~MaterialLibrary() = delete;

    public:
        static Material* add(ShaderID shader, const std::string& name = "Material");

        // 按名称查询，不存在返回 nullptr
        static Material* get(const std::string& name);
        static bool      contains(const std::string& name);

        // 移除材质
        static void      remove(const std::string& name);
        static void      clear();

        // 返回当前注册的材质数量
        static size_t    size();

        // 获取所有已注册材质（按注册顺序），供 Inspector 材质下拉框等使用
        static std::vector<Material*> get_all();

        static Json         serialize(const std::string& name, ArenaID arena_id);
        static Material*    deserialize(const Json& json);
    private:
        static std::vector<std::unique_ptr<Material>>& storage();
    };
} // namespace ID