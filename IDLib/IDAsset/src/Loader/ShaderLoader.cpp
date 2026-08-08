#include "Loader/ShaderLoader.hpp"
#include "Log.hpp"

#include <fstream>
#include <filesystem>
#include <sstream>

namespace
{
    std::string read_file(const std::string& path)
    {
        std::ifstream file(path);
        if(!file.is_open())
        {
            IDASSET_ERROR("ShaderLoader::read_file：无法打开文件: {}", path);
            return "";
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        return oss.str();
    }
} // 匿名命名空间

namespace ID
{
    Asset<ShaderData> ShaderLoader::load(const std::string& vs_path, const std::string& fs_path)
    {
        Asset<ShaderData> asset;

        ShaderData data;
        data.vertex_source = read_file(vs_path);
        data.fragment_source = read_file(fs_path);
        data.vs_path = vs_path;
        data.fs_path = fs_path;

        if(data.is_valid())
        {
            asset.data = std::move(data);
            asset.set_loaded();
        }
        else
        {
            asset.set_failed();
        }

        return asset;
    }

    void ShaderLoader::reload(Asset<ShaderData>& asset)
    {
        if(asset.path.empty())
        {
            IDASSET_ERROR("ShaderLoader::reload：资源路径为空");
            asset.set_failed();
            return;
        }

        asset.reset();
        ShaderData data;
        data.vertex_source = read_file(asset.data.vs_path);
        data.fragment_source = read_file(asset.data.fs_path);
        data.vs_path = asset.data.vs_path;
        data.fs_path = asset.data.fs_path;

        if(data.is_valid())
        {
            asset.data = std::move(data);
            asset.set_loaded();
        }
        else
        {
            asset.set_failed();
        }
    }
} // namespace ID