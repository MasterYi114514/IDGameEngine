#pragma once

#include "IDAssetCore.hpp"
#include "Asset/Asset.hpp"
#include "Asset/AssetPtr.hpp"
#include "Loader/AudioLoader.hpp"
#include "Loader/MaterialLoader.hpp"
#include "Loader/MeshLoader.hpp"
#include "Loader/ShaderLoader.hpp"
#include "Loader/TextureLoader.hpp"

#include <string>
#include <unordered_map>
#include <mutex>

// X-Macro 资源类型列表：
// X(DataType, LoaderClass, funcSuffix, mapSuffix)
// 新增资源类型只需在此追加一行，即可自动获得 load/reload/map/mutex 全套支持
#define ID_ASSET_TYPE_LIST(X)                           \
    X(TextureData,  TextureLoader,  texture,  textures) \
    X(AudioData,    AudioLoader,    audio,    audios)   \
    X(RawMeshData,  MeshLoader,     mesh,     meshes)   \
    X(MaterialData, MaterialLoader, material, materials)

// 每个资源类型的成员声明（由 ID_ASSET_TYPE_LIST 展开）
#define ID_ASSET_DECLARE_TYPE(DataType, LoaderClass, funcSuffix, mapSuffix) \
    using DataType##Asset = Asset<DataType>;                                \
    static AssetPtr<DataType> load_##funcSuffix(const std::string& path);   \
    static void reload_##funcSuffix(AssetPtr<DataType>& asset_ptr);         \
    static void reload_##funcSuffix(const std::string& path);               \
    static std::unordered_map<std::string, Asset<DataType>> s_##mapSuffix;  \
    static std::mutex s_##mapSuffix##_mutex;

namespace ID
{
    class IDASSET_API AssetLibrary
    {
    public:
        AssetLibrary() = delete;
        ~AssetLibrary() = delete;

    public:
        // 各资源类型管理（Texture / Audio / Mesh / Material）
        ID_ASSET_TYPE_LIST(ID_ASSET_DECLARE_TYPE)

    public:
        // Shader 资源管理（双路径签名，不兼容单路径宏，单独声明）
        static AssetPtr<ShaderData> load_shader(const std::string& vs_path, const std::string& fs_path);
        static void reload_shader(AssetPtr<ShaderData>& asset_ptr);
        static void reload_shader(const std::string& vs_path, const std::string& fs_path);

    public:
        // 通用资产管理
        static bool is_loaded(const std::string& path);
        static void unload(const std::string& path);
        static void clear();
        static size_t size();

        // 统一使用正斜杠，去除重复斜杠
        static std::string normalize_path(const std::string& path);

    private:
        static std::unordered_map<std::string, Asset<ShaderData>> s_shaders;
        static std::mutex s_shader_mutex;
    };
} // namespace ID

#undef ID_ASSET_DECLARE_TYPE
#undef ID_ASSET_TYPE_LIST
