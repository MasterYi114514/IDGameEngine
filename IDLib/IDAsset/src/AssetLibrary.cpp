#include "Asset/AssetLibrary.hpp"
#include "Log.hpp"

#include <filesystem>

// 与 AssetLibrary.hpp 中的定义保持一致（头文件展开声明后已 #undef，此处重新定义供实现展开）
#define ID_ASSET_TYPE_LIST(X)                           \
    X(TextureData,  TextureLoader,  texture,  textures) \
    X(AudioData,    AudioLoader,    audio,    audios)   \
    X(RawMeshData,  MeshLoader,     mesh,     meshes)   \
    X(MaterialData, MaterialLoader, material, materials)

namespace ID
{
    // 每个资源类型的静态存储定义
    #define ID_ASSET_DEFINE_STORAGE(DataType, LoaderClass, funcSuffix, mapSuffix) \
        std::unordered_map<std::string, Asset<DataType>> AssetLibrary::s_##mapSuffix; \
        std::mutex AssetLibrary::s_##mapSuffix##_mutex;

    ID_ASSET_TYPE_LIST(ID_ASSET_DEFINE_STORAGE)

    #undef ID_ASSET_DEFINE_STORAGE

    std::unordered_map<std::string, Asset<ShaderData>> AssetLibrary::s_shaders;
    std::mutex AssetLibrary::s_shader_mutex;

    std::string AssetLibrary::normalize_path(const std::string& path)
    {
        std::filesystem::path p(path);
        std::string normalized = p.lexically_normal().generic_string();
        return normalized;
    }

    // load_X(path)：查缓存 → Loader 加载 → 入库，返回 AssetPtr
    #define ID_ASSET_IMPL_LOAD(DataType, LoaderClass, funcSuffix, mapSuffix)                          \
        AssetPtr<DataType> AssetLibrary::load_##funcSuffix(const std::string& path)                   \
        {                                                                                             \
            std::string key = normalize_path(path);                                                   \
            std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);                                  \
                                                                                                      \
            auto it = s_##mapSuffix.find(key);                                                        \
            if(it != s_##mapSuffix.end() && it->second.is_loaded())                                   \
            {                                                                                         \
                return AssetPtr<DataType>(&it->second);                                               \
            }                                                                                         \
                                                                                                      \
            Asset<DataType> asset = IAssetLoader<DataType>::load(path);                               \
            if(asset.is_loaded())                                                                     \
            {                                                                                         \
                std::filesystem::path fs_path(path);                                                  \
                asset.name = fs_path.stem().string();                                                 \
                asset.path = key;                                                                     \
                                                                                                      \
                s_##mapSuffix[key] = std::move(asset);                                                \
                return AssetPtr<DataType>(&s_##mapSuffix[key]);                                       \
            }                                                                                         \
            else                                                                                      \
            {                                                                                         \
                IDASSET_ERROR("AssetLibrary::load_" #funcSuffix "：加载" #DataType "失败: {}", key);  \
                return AssetPtr<DataType>(nullptr);                                                   \
            }                                                                                         \
        }

    ID_ASSET_TYPE_LIST(ID_ASSET_IMPL_LOAD)

    #undef ID_ASSET_IMPL_LOAD

    // reload_X(AssetPtr&)：按指针内 path 重载
    #define ID_ASSET_IMPL_RELOAD_PTR(DataType, LoaderClass, funcSuffix, mapSuffix)                    \
        void AssetLibrary::reload_##funcSuffix(AssetPtr<DataType>& asset_ptr)                         \
        {                                                                                             \
            if(!asset_ptr.is_valid())                                                                 \
            {                                                                                         \
                IDASSET_ERROR("AssetLibrary::reload_" #funcSuffix "：无效的" #DataType "资源指针");   \
                return;                                                                               \
            }                                                                                         \
                                                                                                      \
            std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);                                  \
            std::string key = normalize_path(asset_ptr->path);                                        \
            auto it = s_##mapSuffix.find(key);                                                        \
            if(it == s_##mapSuffix.end())                                                             \
            {                                                                                         \
                IDASSET_ERROR("AssetLibrary::reload_" #funcSuffix "：" #DataType "资源未找到: {}", key); \
                return;                                                                               \
            }                                                                                         \
                                                                                                      \
            it->second.reset();                                                                       \
            Asset<DataType> asset = IAssetLoader<DataType>::load(key);                                \
            asset.name = it->second.name;  /* 保留原来的名称 */                                       \
            if(asset.is_loaded())                                                                     \
            {                                                                                         \
                it->second = std::move(asset);                                                        \
            }                                                                                         \
        }

    ID_ASSET_TYPE_LIST(ID_ASSET_IMPL_RELOAD_PTR)

    #undef ID_ASSET_IMPL_RELOAD_PTR

    // reload_X(path)：按路径重载
    #define ID_ASSET_IMPL_RELOAD_PATH(DataType, LoaderClass, funcSuffix, mapSuffix)                   \
        void AssetLibrary::reload_##funcSuffix(const std::string& path)                               \
        {                                                                                             \
            std::string key = normalize_path(path);                                                   \
            std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);                                  \
            auto it = s_##mapSuffix.find(key);                                                        \
            if(it == s_##mapSuffix.end())                                                             \
            {                                                                                         \
                IDASSET_ERROR("AssetLibrary::reload_" #funcSuffix "：" #DataType "资源未找到: {}", key); \
                return;                                                                               \
            }                                                                                         \
                                                                                                      \
            it->second.reset();                                                                       \
            Asset<DataType> asset = IAssetLoader<DataType>::load(key);                                \
            asset.name = it->second.name;  /* 保留原来的名称 */                                       \
            if(asset.is_loaded())                                                                     \
            {                                                                                         \
                it->second = std::move(asset);                                                        \
            }                                                                                         \
        }

    ID_ASSET_TYPE_LIST(ID_ASSET_IMPL_RELOAD_PATH)

    #undef ID_ASSET_IMPL_RELOAD_PATH

    // Shader 资源管理：双路径签名，key 用 "vs|fs" 拼接（与 is_loaded / unload 内部格式一致）
    AssetPtr<ShaderData> AssetLibrary::load_shader(const std::string& vs_path, const std::string& fs_path)
    {
        std::string key = normalize_path(vs_path) + "|" + normalize_path(fs_path);
        std::lock_guard<std::mutex> lock(s_shader_mutex);

        auto it = s_shaders.find(key);
        if(it != s_shaders.end() && it->second.is_loaded())
        {
            return AssetPtr<ShaderData>(&it->second);
        }

        ShaderLoader loader;
        Asset<ShaderData> asset = loader.load(vs_path, fs_path);
        if(asset.is_loaded())
        {
            std::filesystem::path fs_path_p(vs_path);
            asset.name = fs_path_p.stem().string();
            asset.path = key;

            s_shaders[key] = std::move(asset);
            return AssetPtr<ShaderData>(&s_shaders[key]);
        }
        else
        {
            IDASSET_ERROR("AssetLibrary::load_shader：加载 Shader 失败: {}", key);
            return AssetPtr<ShaderData>(nullptr);
        }
    }

    void AssetLibrary::reload_shader(AssetPtr<ShaderData>& asset_ptr)
    {
        if(!asset_ptr.is_valid())
        {
            IDASSET_ERROR("AssetLibrary::reload_shader：无效的 Shader 资源指针");
            return;
        }

        std::lock_guard<std::mutex> lock(s_shader_mutex);
        std::string key = normalize_path(asset_ptr->data.vs_path) + "|" + normalize_path(asset_ptr->data.fs_path);
        auto it = s_shaders.find(key);
        if(it == s_shaders.end())
        {
            IDASSET_ERROR("AssetLibrary::reload_shader：Shader 资源未找到: {}", key);
            return;
        }

        // 先保存路径（reset 会清空 data，而 asset_ptr 指向 it->second）
        std::string vs_path = asset_ptr->data.vs_path;
        std::string fs_path = asset_ptr->data.fs_path;

        it->second.reset();
        ShaderLoader loader;
        Asset<ShaderData> asset = loader.load(vs_path, fs_path);
        asset.name = it->second.name;  /* 保留原来的名称 */
        asset.path = key;             /* 保持 key 与 load_shader 一致 */
        if(asset.is_loaded())
        {
            it->second = std::move(asset);
        }
    }

    void AssetLibrary::reload_shader(const std::string& vs_path, const std::string& fs_path)
    {
        std::string key = normalize_path(vs_path) + "|" + normalize_path(fs_path);
        std::lock_guard<std::mutex> lock(s_shader_mutex);
        auto it = s_shaders.find(key);
        if(it == s_shaders.end())
        {
            IDASSET_ERROR("AssetLibrary::reload_shader：Shader 资源未找到: {}", key);
            return;
        }

        it->second.reset();
        ShaderLoader loader;
        Asset<ShaderData> asset = loader.load(vs_path, fs_path);
        asset.name = it->second.name;  /* 保留原来的名称 */
        asset.path = key;             /* 保持 key 与 load_shader 一致 */
        if(asset.is_loaded())
        {
            it->second = std::move(asset);
        }
    }

    bool AssetLibrary::is_loaded(const std::string& path)
    {
        std::string key = normalize_path(path);

        // 遍历所有资源类型的 map
        #define ID_ASSET_IS_LOADED(DataType, LoaderClass, funcSuffix, mapSuffix)  \
            {                                                                     \
                std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);          \
                auto it = s_##mapSuffix.find(key);                                \
                if(it != s_##mapSuffix.end() && it->second.is_loaded())           \
                {                                                                 \
                    return true;                                                  \
                }                                                                 \
            }

        ID_ASSET_TYPE_LIST(ID_ASSET_IS_LOADED)

        #undef ID_ASSET_IS_LOADED

        // Shader：key 为 "vs|fs" 拼接格式
        {
            std::lock_guard<std::mutex> lock(s_shader_mutex);
            auto it = s_shaders.find(key);
            if(it != s_shaders.end() && it->second.is_loaded())
            {
                return true;
            }
        }

        return false;
    }

    void AssetLibrary::unload(const std::string& path)
    {
        std::string key = normalize_path(path);

        // 遍历所有资源类型的 map 删除
        #define ID_ASSET_UNLOAD(DataType, LoaderClass, funcSuffix, mapSuffix)     \
            {                                                                     \
                std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);          \
                auto it = s_##mapSuffix.find(key);                                \
                if(it != s_##mapSuffix.end())                                     \
                {                                                                 \
                    s_##mapSuffix.erase(it);                                      \
                }                                                                 \
            }

        ID_ASSET_TYPE_LIST(ID_ASSET_UNLOAD)

        #undef ID_ASSET_UNLOAD

        // Shader：key 为 "vs|fs" 拼接格式
        {
            std::lock_guard<std::mutex> lock(s_shader_mutex);
            auto it = s_shaders.find(key);
            if(it != s_shaders.end())
            {
                s_shaders.erase(it);
            }
        }
    }

    void AssetLibrary::clear()
    {
        // 遍历所有资源类型的 map 清空
        #define ID_ASSET_CLEAR(DataType, LoaderClass, funcSuffix, mapSuffix)      \
            {                                                                     \
                std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);          \
                s_##mapSuffix.clear();                                            \
            }

        ID_ASSET_TYPE_LIST(ID_ASSET_CLEAR)

        #undef ID_ASSET_CLEAR

        std::lock_guard<std::mutex> lock(s_shader_mutex);
        s_shaders.clear();
    }

    size_t AssetLibrary::size()
    {
        size_t total = 0;

        // 汇总所有资源类型的 map size
        #define ID_ASSET_SIZE(DataType, LoaderClass, funcSuffix, mapSuffix)       \
            {                                                                     \
                std::lock_guard<std::mutex> lock(s_##mapSuffix##_mutex);          \
                total += s_##mapSuffix.size();                                    \
            }

        ID_ASSET_TYPE_LIST(ID_ASSET_SIZE)

        #undef ID_ASSET_SIZE

        {
            std::lock_guard<std::mutex> lock(s_shader_mutex);
            total += s_shaders.size();
        }
        return total;
    }
} // namespace ID

#undef ID_ASSET_TYPE_LIST
