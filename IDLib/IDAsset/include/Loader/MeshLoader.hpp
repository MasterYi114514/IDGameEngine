#pragma once

#include "IDAssetCore.hpp"
#include "Asset/Asset.hpp"
#include "Loader/IAssetLoader.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace ID
{
    struct RawMeshData
    {
        std::vector<float>      vertices_data;      // 交错顶点数据（pos + uv + normal）
        std::vector<uint32_t>   indices;
        uint32_t                vertex_count = 0;
        uint32_t                index_count = 0;

        bool is_valid() const { return vertex_count > 0 && !vertices_data.empty(); }
    };

    struct MeshLoadResult
    {
        std::vector<RawMeshData>    meshes;             // Mesh 列表
        std::vector<std::string>    mesh_names;         // Mesh 名称
        bool success = false;
    };

    template<>
    class IDASSET_API IAssetLoader<RawMeshData>
    {
    public:
        static Asset<RawMeshData> load(const std::string& path);
        static void reload(Asset<RawMeshData>& asset);

        /**
         *  加载路径下的所有网格数据，返回 MeshLoadResult
         */
        static MeshLoadResult load_meshes(const std::string& path);
    };

    using MeshLoader = IAssetLoader<RawMeshData>;
} // namespace ID