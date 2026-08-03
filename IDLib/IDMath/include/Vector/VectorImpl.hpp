#pragma once

#include <cmath>

#include "Core/IDMathConcepts.hpp"
#include "Core/HeaderLogger.hpp"
#include "Vector/Vector.hpp"

// Vector 相关函数的具体实现都在此文件夹中

namespace ID
{
#ifdef IDMATH_USE_GLM

    // 低维向量 → 高维向量构造
    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    template<std::size_t N, typename... ExtraElems>
    requires (N < Dim) && (sizeof...(ExtraElems) == Dim - N) && (std::convertible_to<ExtraElems, T> && ...)
    Vector<T, Dim>::Vector(const Vector<T, N>& vec, ExtraElems... extra_elems)
        : m_vector(vec.get_glm_vector(), static_cast<T>(extra_elems)...)
    { }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    void Vector<T, Dim>::normalize() requires Math::IsDecimal<T>
    {
        if(this->is_zero())
        {
            HEAD_ERROR("尝试对零向量进行单位化，操作已被忽略");
            return;
        }
        m_vector = glm::normalize(m_vector);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    T Vector<T, Dim>::get_length() const requires Math::IsDecimal<T>
    {
        return glm::length(m_vector);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    bool Vector<T, Dim>::is_zero(T* len) const requires Math::IsDecimal<T>
    {
        static double epsilon = 1e-6;
        T length = glm::length(m_vector);
        if(len) *len = length;
        return length < epsilon;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    T Vector<T, Dim>::dot(const Vector& a, const Vector& b) requires Math::IsDecimal<T>
    {
        return glm::dot(a.m_vector, b.m_vector);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::cross(const Vector& a, const Vector& b) requires Math::Crossable<T, Dim>
    {
        Vector result;
        result.m_vector = glm::cross(a.m_vector, b.m_vector);
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    T Vector<T, Dim>::dot(const Vector& other) const requires Math::IsDecimal<T>
    {
        return dot(*this, other);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::cross(const Vector& other) const requires Math::Crossable<T, Dim>
    {
        return cross(*this, other);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator+(const Vector& other) const requires Math::IsDecimal<T>
    {
        Vector result;
        result.m_vector = m_vector + other.m_vector;
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator-(const Vector& other) const requires Math::IsDecimal<T>
    {
        Vector result;
        result.m_vector = m_vector - other.m_vector;
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator*(float scalar) const requires Math::IsDecimal<T>
    {
        Vector result;
        result.m_vector = m_vector * scalar;
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator/(float scalar) const requires Math::IsDecimal<T>
    {
        if(scalar == 0.0f)
        {
            HEAD_ERROR("尝试除以零，操作已被忽略");
            return *this;
        }
        Vector result;
        result.m_vector = m_vector / scalar;
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator*(const Vector& other) const requires Math::IsDecimal<T>
    {
        Vector result;
        result.m_vector = m_vector * other.m_vector;
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator+=(const Vector& other) requires Math::IsDecimal<T>
    {
        m_vector += other.m_vector;
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator-=(const Vector& other) requires Math::IsDecimal<T>
    {
        m_vector -= other.m_vector;
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator*=(float scalar) requires Math::IsDecimal<T>
    {
        m_vector *= scalar;
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator/=(float scalar) requires Math::IsDecimal<T>
    {
        if(scalar == 0.0f)
        {
            HEAD_ERROR("尝试除以零，操作已被忽略");
            return *this;
        }
        m_vector /= scalar;
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator*=(const Vector& other) requires Math::IsDecimal<T>
    {
        m_vector *= other.m_vector;
        return *this;
    }

#else 
    // 低维向量 → 高维向量构造
    template<typename T, std::size_t Dim>
    template<std::size_t N, typename... ExtraElems>
    requires (N < Dim) && (sizeof...(ExtraElems) == Dim - N) && (std::convertible_to<ExtraElems, T> && ...)
    Vector<T, Dim>::Vector(const Vector<T, N>& vec, ExtraElems... extra_elems)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            m_array[i] = vec[i];
        }
        init(N, static_cast<T>(extra_elems)...);
    }

    // 析构函数 与 拷贝、移动构造 ----------------------------------------------------------------------------------------
    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>::~Vector()
    {
        delete[] m_array;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>::Vector(const Vector& other)
    {
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] = other.m_array[i];
        }
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator=(const Vector& other)
    {
        if (this != &other)
        {
            for (std::size_t i = 0; i < s_dimension; ++i)
            {
                m_array[i] = other.m_array[i];
            }
        }
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>::Vector(Vector&& other) noexcept : m_array(other.m_array)
    {
        other.m_array = nullptr;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator=(Vector&& other) noexcept
    {
        if(this != &other)
        {
            delete[] m_array;
            m_array = other.m_array;
            other.m_array = nullptr;
        }
        return *this;
    }

    // 对向量操作的支持 ------------------------------------------------------------------------------------------------
    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    void Vector<T, Dim>::normalize() requires Math::IsDecimal<T>
    {
        T length;
        if(this->is_zero(&length))
        {
            HEAD_ERROR("尝试对零向量进行单位化，操作已被忽略");
            return;
        }

        for(std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] /= length;
        }
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    T Vector<T, Dim>::get_length() const requires Math::IsDecimal<T>
    {
        T sum = T(0);
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            sum += m_array[i] * m_array[i];
        }
        return std::sqrt(sum);
    }

    /*
        判断向量的长度是否为零
        内部会调用 get_length() 来计算向量的长度
        因此，允许传入一个参数 len，用于获取向量的长度
        防止重复计算，造成性能浪费
    */
    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    bool Vector<T, Dim>::is_zero(T* len) const requires Math::IsDecimal<T>
    {
        static double epsilon = 1e-6;

        T length = this->get_length();
        if(len) *len = length;

        return length < epsilon;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    T Vector<T, Dim>::dot(const Vector& a, const Vector& b) requires Math::IsDecimal<T>
    {
        T result = T(0);
        for (std::size_t i = 0; i < Dim; ++i)
        {
            result += a[i] * b[i];
        }
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::cross(const Vector& a, const Vector& b) requires Math::Crossable<T, Dim>
    {
        Vector<T, Dim> result;
        result[0] = a[1] * b[2] - a[2] * b[1];
        result[1] = a[2] * b[0] - a[0] * b[2];
        result[2] = a[0] * b[1] - a[1] * b[0];
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    T Vector<T, Dim>::dot(const Vector& other) const requires Math::IsDecimal<T>
    {
        return dot(*this, other);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::cross(const Vector& other) const requires Math::Crossable<T, Dim>
    {
        return cross(*this, other);
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator+(const Vector& other) const requires Math::IsDecimal<T>
    {
        Vector result;
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            result[i] = m_array[i] + other.m_array[i];
        }
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator-(const Vector& other) const requires Math::IsDecimal<T>
    {
        Vector result;
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            result[i] = m_array[i] - other.m_array[i];
        }
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator*(float scalar) const requires Math::IsDecimal<T>
    {
        Vector result;
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            result[i] = m_array[i] * scalar;
        }
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator/(float scalar) const requires Math::IsDecimal<T>
    {
        if (scalar == 0.0f)
        {
            HEAD_ERROR("尝试除以零，操作已被忽略");
            return *this;
        }
        Vector result;
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            result[i] = m_array[i] / scalar;
        }
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim> Vector<T, Dim>::operator*(const Vector& other) const requires Math::IsDecimal<T>
    {
        Vector result;
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            result[i] = m_array[i] * other.m_array[i];
        }
        return result;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator+=(const Vector& other) requires Math::IsDecimal<T>
    {
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] += other.m_array[i];
        }
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator-=(const Vector& other) requires Math::IsDecimal<T>
    {
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] -= other.m_array[i];
        }
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator*=(float scalar) requires Math::IsDecimal<T>
    {
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] *= scalar;
        }
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator/=(float scalar) requires Math::IsDecimal<T>
    {
        if (scalar == 0.0f)
        {
            HEAD_ERROR("尝试除以零，操作已被忽略");
            return *this;
        }
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] /= scalar;
        }
        return *this;
    }

    template<typename T, std::size_t Dim>
    requires Math::CanBeVector<T, Dim>
    Vector<T, Dim>& Vector<T, Dim>::operator*=(const Vector& other) requires Math::IsDecimal<T>
    {
        for (std::size_t i = 0; i < s_dimension; ++i)
        {
            m_array[i] *= other.m_array[i];
        }
        return *this;
    }

#endif
} // namespace ID