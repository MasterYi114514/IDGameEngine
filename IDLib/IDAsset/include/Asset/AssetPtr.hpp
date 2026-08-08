#pragma once

#include "Asset/Asset.hpp"

namespace ID
{
    /**
     *  对 Asset<T> 的包装，提供更安全的访问语义
     *  并不拥有 Asset<T> 的所有权
     */
    template<typename T>
    struct AssetPtr
    {
        Asset<T>* asset = nullptr;

        AssetPtr() = default;
        AssetPtr(Asset<T>* asset) : asset(asset) { }

        // 指针操作符
        Asset<T>* operator->()              { return asset; }
        const Asset<T>* operator->() const  { return asset; }
        Asset<T>& operator*()               { return *asset; }
        const Asset<T>& operator*()  const  { return *asset; }

        // 合法性判断
        bool is_valid() const { return asset != nullptr && asset->is_loaded(); }
        bool is_failed() const { return asset != nullptr && asset->is_failed(); }

        explicit operator bool() const { return is_valid(); }

        // 重置
        void reset(Asset<T>* new_asset) { asset = new_asset; }
    };
};