#pragma once

#include "IDAssetCore.hpp"
#include "Asset/Asset.hpp"
#include "Loader/IAssetLoader.hpp"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace ID
{
    // 资源前向声明
    class Material;
    class MaterialInstance;

    // uniform参数条目
    struct MaterialParamEntry
    {
        std::string name;               // uniform 名称
        std::string type;               // float、vec3、int等
        std::vector<float> value;       // 使用float存储所有类型的值，具体类型由type字段决定
    };

    // 纹理条目
    struct MaterialTextureEntry
    {
        std::string sampler_name;          // 纹理采样器的 uniform 名称
        std::string texture_path;          // 纹理文件路径
        uint32_t    slot = 0;              // 纹理绑定槽位
    };

    struct MaterialData
    {
        std::string shader_name;                        // shader的名称
        std::vector<MaterialParamEntry> params;         // uniform参数列表
        std::vector<MaterialTextureEntry> textures;     // 纹理列表
        bool transparent = false;                       // 是否透明

        bool is_valid() const { return !shader_name.empty(); }
    };

    /**
     *  .mat JSON 文件格式：
     *  {
     *      "shader": "geometry",
     *      "params": {
     *          "u_color": [1.0, 0.5, 0.2],
     *          "u_roughness": 0.5
     *      },
     *      "textures": {
     *          "texture_sampler": "Assets/texture/player.png"
     *      },
     *      "transparent": false
     *  }
     */
    class IDASSET_API MaterialLoader
    {
    public:
        // 从 .mat 文件加载 MaterialData
        static MaterialData load(const std::string& path);

        // 将 MaterialData 保存为 .mat 文件
        static void save(const MaterialData& data, const std::string& path);
    };

    // 特化：使 MaterialLoader 遵循 IAssetLoader<T> 统一接口
    template<>
    class IDASSET_API IAssetLoader<MaterialData>
    {
    public:
        static Asset<MaterialData> load(const std::string& path);
        static void reload(Asset<MaterialData>& asset);
    };
} // namespace ID