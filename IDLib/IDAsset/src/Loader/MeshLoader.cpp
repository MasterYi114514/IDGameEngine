#include "Loader/MeshLoader.hpp"

#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>

#include "Log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace
{
    ID::MeshLoadResult load_assimp(const std::string& path)
    {
        ID::MeshLoadResult result;

        if (!std::filesystem::exists(path))
        {
            IDASSET_ERROR("MeshLoader::load_meshes：文件不存在: {}", path);
            return result;
        }

        Assimp::Importer importer;

        // 导入选项：
        //  - aiProcess_Triangulate：强制三角形
        //  - aiProcess_GenNormals：无法线时自动生成
        //  - aiProcess_FlipUVs：翻转 UV（OpenGL 约定）
        //  - aiProcess_JoinIdenticalVertices：合并重复顶点
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_FlipUVs |
            aiProcess_JoinIdenticalVertices);

        if (!scene || !scene->HasMeshes())
        {
            IDASSET_ERROR("MeshLoader::load_meshes：无法加载网格: {}", std::string(importer.GetErrorString()));
            return result;
        }

        result.success = true;

        for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
        {
            aiMesh* ai_mesh = scene->mMeshes[mi];

            ID::RawMeshData mesh_data;
            mesh_data.vertex_count = ai_mesh->mNumVertices;

            // ── 构建交错顶点数据（pos + uv + normal = 8 floats = 32 bytes）──
            // 与 MeshFactory::get_layout() 的 aPos+aUV+aNormal 对齐
            bool has_uv = ai_mesh->HasTextureCoords(0);
            bool has_normal = ai_mesh->HasNormals();

            mesh_data.vertices_data.reserve(ai_mesh->mNumVertices * 8);

            for (unsigned int vi = 0; vi < ai_mesh->mNumVertices; ++vi)
            {
                // Position (3 floats)
                mesh_data.vertices_data.push_back(ai_mesh->mVertices[vi].x);
                mesh_data.vertices_data.push_back(ai_mesh->mVertices[vi].y);
                mesh_data.vertices_data.push_back(ai_mesh->mVertices[vi].z);

                // UV (2 floats)
                if (has_uv)
                {
                    mesh_data.vertices_data.push_back(ai_mesh->mTextureCoords[0][vi].x);
                    mesh_data.vertices_data.push_back(ai_mesh->mTextureCoords[0][vi].y);
                }
                else
                {
                    mesh_data.vertices_data.push_back(0.0f);
                    mesh_data.vertices_data.push_back(0.0f);
                }

                // Normal (3 floats)
                if (has_normal)
                {
                    mesh_data.vertices_data.push_back(ai_mesh->mNormals[vi].x);
                    mesh_data.vertices_data.push_back(ai_mesh->mNormals[vi].y);
                    mesh_data.vertices_data.push_back(ai_mesh->mNormals[vi].z);
                }
                else
                {
                    mesh_data.vertices_data.push_back(0.0f);
                    mesh_data.vertices_data.push_back(1.0f);
                    mesh_data.vertices_data.push_back(0.0f);
                }
            }

            // ── 索引数据 ──
            mesh_data.index_count = ai_mesh->mNumFaces * 3;  // 三角形
            mesh_data.indices.reserve(mesh_data.index_count);

            for (unsigned int fi = 0; fi < ai_mesh->mNumFaces; ++fi)
            {
                aiFace& face = ai_mesh->mFaces[fi];
                assert(face.mNumIndices == 3);  // 已 triangulate

                mesh_data.indices.push_back(face.mIndices[0]);
                mesh_data.indices.push_back(face.mIndices[1]);
                mesh_data.indices.push_back(face.mIndices[2]);
            }

            // ── diffuse 纹理路径提取 ──
            if(ai_mesh->mMaterialIndex < scene->mNumMaterials)
            {
                aiMaterial* ai_mat = scene->mMaterials[ai_mesh->mMaterialIndex];
                aiString tex_path;
                if(ai_mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path) == AI_SUCCESS)
                {
                    // 嵌入纹理（"*0" 开头）：本计划不支持导出，留空
                    if(tex_path.length > 0 && tex_path.C_Str()[0] != '*')
                    {
                        mesh_data.texture_path = std::string(tex_path.C_Str());
                    }
                }
            }

            // 路径规范化：绝对路径直接使用，相对路径拼接模型文件所在目录
            if(!mesh_data.texture_path.empty())
            {
                std::filesystem::path tex_path(mesh_data.texture_path);
                if(!tex_path.is_absolute())
                {
                    mesh_data.texture_path =
                        (std::filesystem::path(path).parent_path() / tex_path).lexically_normal().string();
                }
            }

            // 无 UV 保护：顶点无 UV 时采样纹理无意义，强制置空避免下游误绑
            if(!has_uv && !mesh_data.texture_path.empty())
            {
                IDASSET_WARN("MeshLoader::load_meshes：submesh '{}' 无 UV，忽略纹理路径: {}",
                    ai_mesh->mName.length > 0 ? ai_mesh->mName.C_Str() : "submesh_" + std::to_string(mi),
                    mesh_data.texture_path);
                mesh_data.texture_path.clear();
            }

            result.meshes.push_back(std::move(mesh_data));
            result.mesh_names.push_back(
                ai_mesh->mName.length > 0 ? ai_mesh->mName.C_Str() : "submesh_" + std::to_string(mi));
        }

        return result;
    }
} // 匿名命名空间

namespace ID
{
    MeshLoadResult MeshLoader::load_meshes(const std::string& path)
    {
        return load_assimp(path);
    }

    Asset<RawMeshData> MeshLoader::load(const std::string& path)
    {
        Asset<RawMeshData> asset;
        MeshLoadResult result = load_assimp(path);
        if(result.success && !result.meshes.empty())
        {
            asset.data = std::move(result.meshes[0]);
            asset.set_loaded();
        }
        else
        {
            asset.set_failed();
        }
        return asset;
    }

    void MeshLoader::reload(Asset<RawMeshData>& asset)
    {
        if(asset.path.empty())
        {
            IDASSET_ERROR("MeshLoader::reload：资源路径为空");
            asset.set_failed();
            return;
        }

        asset.reset();
        Asset<RawMeshData> new_asset = load(asset.path);
        if(new_asset.is_loaded())
        {
            asset.data = std::move(new_asset.data);
            asset.set_loaded();
        }
        else
        {
            asset.set_failed();
        }
    }
}