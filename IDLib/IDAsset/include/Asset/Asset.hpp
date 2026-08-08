#pragma once

#include <string>
#include <vector>

namespace ID
{
    enum class AssetState
    {
        Unloaded,       // 资源未加载
        Loading,        // 资源正在加载中
        Loaded,         // 资源已加载
        Failed          // 资源加载失败
    };

    template<typename T>
    struct Asset
    {
        AssetState  state = AssetState::Unloaded;
        T           data;
        std::string path;
        std::string name;

        bool is_loaded() const { return state == AssetState::Loaded; }
        bool is_failed() const { return state == AssetState::Failed; }

        void set_loaded() { state = AssetState::Loaded; }
        void set_failed() { state = AssetState::Failed; }
        void reset() { state = AssetState::Unloaded; data = T{}; }
    };
} // namespace ID