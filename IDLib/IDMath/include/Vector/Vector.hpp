#pragma once

#include <cstddef>

#ifdef IDMATH_USE_GLM
    #include <glm/glm.hpp>
#endif

#include "Core/IDMathCore.hpp"
#include "Core/IDMathConcepts.hpp"
#include "Core/HeaderLogger.hpp"

namespace ID
{
#ifdef IDMATH_USE_GLM
    /*
    *   底层用 glm 库实现的向量类
    *     - 只允许 2、3、4 维的向量类型
    *     - 向量的元素类型必须是 float、double、int 或 bool
    */
    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    class IDMATH_API Vector
    {
        using GlmType = glm::vec<Dim, T>;
        using ValueType = T;
    public:
        Vector() = default;

        template<typename... Elems>
        requires (sizeof...(Elems) == Dim) && (std::convertible_to<Elems, T> && ...)
        explicit(sizeof...(Elems) != 1)
        Vector(Elems... elems) : m_vector(static_cast<T>(elems)...) { }

        // 由低维向量构造高维向量
        template<std::size_t N, typename... ExtraElems>
        requires (N < Dim) && (sizeof...(ExtraElems) == Dim - N) && (std::convertible_to<ExtraElems, T> && ...)
        Vector(const Vector<T, N>& vec, ExtraElems... extra_elems);

        // 从 glm 向量构造
        explicit Vector(const GlmType& v) : m_vector(v) { }

        // 元素访问
        T& operator[](std::size_t i) { return m_vector[i]; }
        const T& operator[](std::size_t i) const { return m_vector[i]; }

        // 拷贝
        Vector(const Vector&)            = default;
        Vector& operator=(const Vector&) = default;

        // 移动
        Vector(Vector&&)            = default;
        Vector& operator=(Vector&&) = default;
    public:
        // 获取维度
        static constexpr std::size_t get_dimension() { return s_dimension; }

        // 获取底层 glm 向量
        GlmType& get_glm_vector() { return m_vector; }
        const GlmType& get_glm_vector() const { return m_vector; }

    protected:
        static constexpr std::size_t s_dimension = Dim;
        GlmType m_vector;

    public:
        // 向量操作，仅支持 T 是 float 或 double 的情况

        void    normalize()                                             requires Math::IsDecimal<T>;
        T       get_length()                                    const   requires Math::IsDecimal<T>;
        bool    is_zero(T* len = nullptr)                       const   requires Math::IsDecimal<T>;

        T        dot(const Vector& other)                       const   requires Math::IsDecimal<T>;
        static T dot(const Vector& a, const Vector& b)                  requires Math::IsDecimal<T>;
        Vector   cross(const Vector& other)                     const   requires Math::Crossable<T, Dim>;
        static   Vector cross(const Vector& a, const Vector& b)         requires Math::Crossable<T, Dim>;

        Vector  operator+(const Vector& other)                  const   requires Math::IsDecimal<T>;
        Vector  operator-(const Vector& other)                  const   requires Math::IsDecimal<T>;
        Vector  operator*(float scalar)                         const   requires Math::IsDecimal<T>;
        Vector  operator/(float scalar)                         const   requires Math::IsDecimal<T>;
        Vector  operator*(const Vector& other)                  const   requires Math::IsDecimal<T>;

        Vector& operator+=(const Vector& other)                         requires Math::IsDecimal<T>;
        Vector& operator-=(const Vector& other)                         requires Math::IsDecimal<T>;
        Vector& operator*=(float scalar)                                requires Math::IsDecimal<T>;
        Vector& operator/=(float scalar)                                requires Math::IsDecimal<T>;
        Vector& operator*=(const Vector& other)                         requires Math::IsDecimal<T>;

        bool    operator==(const Vector& other)                 const   requires Math::IsDecimal<T>;
        bool    operator!=(const Vector& other)                 const   requires Math::IsDecimal<T>;

    public:
        // 迭代器的实现
        struct Iterator
        {
            T* m_ptr;
            Iterator(T* ptr) : m_ptr(ptr) { }
            Iterator& operator++() { ++m_ptr; return *this; }
            T& operator*() { return *m_ptr; }
            bool operator!=(const Iterator& other) const { return m_ptr != other.m_ptr; }
        };

        struct ConstIterator
        {
            const T* m_ptr;
            ConstIterator(const T* ptr) : m_ptr(ptr) { }
            ConstIterator& operator++() { ++m_ptr; return *this; }
            const T& operator*() { return *m_ptr; }
            bool operator!=(const ConstIterator& other) const { return m_ptr != other.m_ptr; }
        };

        Iterator begin() { return Iterator(&m_vector[0]); }
        Iterator end() { return Iterator(&m_vector[0] + s_dimension); }
        ConstIterator begin() const { return ConstIterator(&m_vector[0]); }
        ConstIterator end() const { return ConstIterator(&m_vector[0] + s_dimension); }
    };

#else

    /*
    *   底层为一维数组实现的向量类
    *     - 允许任意维度的向量类型
    *     - 向量的元素类型可以是任意类型，但传入的参数必须有默认构造函数
    *   其中函数的具体实现见 "Vector/VectorImpl.hpp"
    */
    template<typename T, std::size_t Dim>
    class IDMATH_API Vector
    {
        using ValueType = T;
    public:
        Vector() = default;
        ~Vector();

        template<typename... Elems>
        requires (sizeof...(Elems) == Dim) && (std::convertible_to<Elems, T> && ...)
        Vector(Elems... elems)
        {
            init(0, elems...);
        }

        // 从低维向量 + 额外分量构造高维向量
        template<std::size_t N, typename... ExtraElems>
        requires (N < Dim) && (sizeof...(ExtraElems) == Dim - N) && (std::convertible_to<ExtraElems, T> && ...)
        Vector(const Vector<T, N>& vec, ExtraElems... extra_elems);

        // 元素访问
        T& operator[](std::size_t i) { return m_array[i]; }
        const T& operator[](std::size_t i) const { return m_array[i]; }

        // 拷贝
        Vector(const Vector& other);
        Vector& operator=(const Vector&);

        // 移动
        Vector(Vector&&) noexcept;
        Vector& operator=(Vector&&) noexcept;

    public:
        // 获取维度
        static constexpr std::size_t get_dimension() { return s_dimension; }

    protected:
        static constexpr std::size_t s_dimension = Dim;
        ValueType* m_array = new ValueType[s_dimension]();

        template<typename... Elems>
        void init(size_t pos, ValueType target, Elems... elems)
        {
            m_array[pos] = target;
            if constexpr (sizeof...(elems) > 0)
            {
                init(pos + 1, elems...);
            }
        }

    public:        
        // 向量操作，仅支持 T 是 float 或 double 的情况

        void    normalize()                                             requires Math::IsDecimal<T>;
        T       get_length()                                    const   requires Math::IsDecimal<T>;
        bool    is_zero(T* len = nullptr)                       const   requires Math::IsDecimal<T>;

        T        dot(const Vector& other)                       const   requires Math::IsDecimal<T>;
        static T dot(const Vector& a, const Vector& b)                  requires Math::IsDecimal<T>;
        Vector   cross(const Vector& other)                     const   requires Math::Crossable<T, Dim>;
        static   Vector cross(const Vector& a, const Vector& b)         requires Math::Crossable<T, Dim>;

        Vector  operator+(const Vector& other)                  const   requires Math::IsDecimal<T>;
        Vector  operator-(const Vector& other)                  const   requires Math::IsDecimal<T>;
        Vector  operator*(float scalar)                         const   requires Math::IsDecimal<T>;
        Vector  operator/(float scalar)                         const   requires Math::IsDecimal<T>;
        Vector  operator*(const Vector& other)                  const   requires Math::IsDecimal<T>;

        Vector& operator+=(const Vector& other)                         requires Math::IsDecimal<T>;
        Vector& operator-=(const Vector& other)                         requires Math::IsDecimal<T>;
        Vector& operator*=(float scalar)                                requires Math::IsDecimal<T>;
        Vector& operator/=(float scalar)                                requires Math::IsDecimal<T>;
        Vector& operator*=(const Vector& other)                         requires Math::IsDecimal<T>;

        bool    operator==(const Vector& other)                 const   requires Math::IsDecimal<T>;
        bool    operator!=(const Vector& other)                 const   requires Math::IsDecimal<T>;

    public:
        // 迭代器的实现
        struct Iterator
        {
            ValueType* m_ptr;
            Iterator(ValueType* ptr) : m_ptr(ptr) { }
            Iterator& operator++() { ++m_ptr; return *this; }
            ValueType& operator*() { return *m_ptr; }
            bool operator!=(const Iterator& other) { return m_ptr != other.m_ptr; }
        };

        struct ConstIterator
        {
            const ValueType* m_ptr;
            ConstIterator(const ValueType* ptr) : m_ptr(ptr) { }
            ConstIterator& operator++() { ++m_ptr; return *this; }
            const ValueType& operator*() { return *m_ptr; }
            bool operator!=(const ConstIterator& other) { return m_ptr != other.m_ptr; }
        };

        Iterator begin() { return Iterator(m_array); }
        Iterator end() { return Iterator(m_array + s_dimension); }
        ConstIterator begin() const { return ConstIterator(m_array); }
        ConstIterator end() const { return ConstIterator(m_array + s_dimension); }
    };

#endif
} // namespace ID