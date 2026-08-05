#pragma once

#include <cstddef>
#include <cstring>
#include <cassert>

namespace ID
{
    /**
     *  自定义 Array 类，存储固定大小的 float 数组
     *  确保运行零开销
     */
    template<typename T, size_t SIZE>
    struct Array
    {
        T data[SIZE] { };
        static constexpr size_t size = SIZE;
    
        // 构造函数和析构函数
        Array() = default;
        ~Array() = default;

        explicit Array(const T* src)
        {
            if(src)
            std::memcpy(data, src, SIZE * sizeof(T));
        }

        Array(const Array& other)
        {
            std::memcpy(data, other.data, SIZE * sizeof(T));
        }

        Array& operator=(const Array& other)
        {
            if (this != &other)
            {
                std::memcpy(data, other.data, SIZE * sizeof(T));
            }
            return *this;
        }

        // 访问元素
        T*          get_data() { return data; }
        const T*    get_data() const { return data; }

        T&          operator[](size_t index) { assert(index < SIZE); return data[index]; }
        const T&    operator[](size_t index) const { assert(index < SIZE); return data[index]; }

        // 迭代器
        T*          begin() { return data; }
        T*          end() { return data + SIZE; }
        const T*    begin() const { return data; }
        const T*    end() const { return data + SIZE; }
    };
}