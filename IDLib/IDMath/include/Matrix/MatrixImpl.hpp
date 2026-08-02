#include "Matrix/Matrix.hpp"
#include "Vector/Vector.hpp"

#ifdef IDMATH_USE_GLM

namespace ID
{
    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>::Matrix(const Matrix& other)
        : m_glm_mat(other.m_glm_mat)
    {
        m_arow.m_array = m_data;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator=(const Matrix& other)
    {
        if(this != &other)
        {
            m_glm_mat = other.m_glm_mat;
            m_arow.m_array = m_data;
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>::Matrix(Matrix&& other) noexcept
        : m_glm_mat(std::move(other.m_glm_mat))
    {
        m_arow.m_array = m_data;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator=(Matrix&& other) noexcept
    {
        if(this != &other)
        {
            m_glm_mat = std::move(other.m_glm_mat);
            m_arow.m_array = m_data;
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::get_identity() requires Math::IsDecimal<T> && Math::IsSquare<RowsSize, ColsSize>
    {
        Matrix result;
        result.m_glm_mat = glm::mat<ColsSize, RowsSize, T>(static_cast<T>(1));
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::get_transpose() const requires Math::IsDecimal<T>
    {
        Matrix result;
        result.m_glm_mat = glm::transpose(m_glm_mat);
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator+(const Matrix& other) const requires Math::IsDecimal<T>
    {
        Matrix result;
        result.m_glm_mat = m_glm_mat + other.m_glm_mat;
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator-(const Matrix& other) const requires Math::IsDecimal<T>
    {
        Matrix result;
        result.m_glm_mat = m_glm_mat - other.m_glm_mat;
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator*(const Matrix& other) const requires Math::IsDecimal<T>
    {
        Matrix result;
        result.m_glm_mat = m_glm_mat * other.m_glm_mat;
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator*(const float scalar) const requires Math::IsDecimal<T>
    {
        Matrix result;
        result.m_glm_mat = m_glm_mat * scalar;
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator/(const float scalar) const requires Math::IsDecimal<T>
    {
        Matrix result;
        result.m_glm_mat = m_glm_mat / scalar;
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator+=(const Matrix& other) requires Math::IsDecimal<T>
    {
        m_glm_mat += other.m_glm_mat;
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator-=(const Matrix& other) requires Math::IsDecimal<T>
    {
        m_glm_mat -= other.m_glm_mat;
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator*=(const Matrix& other) requires Math::IsDecimal<T>
    {
        m_glm_mat *= other.m_glm_mat;
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator*=(const float scalar) requires Math::IsDecimal<T>
    {
        m_glm_mat *= scalar;
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator/=(const float scalar) requires Math::IsDecimal<T>
    {
        m_glm_mat /= scalar;
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Vector<T, RowsSize> Matrix<T, RowsSize, ColsSize>::operator*(const Vector<T, ColsSize>& vec) const requires Math::IsDecimal<T>
    {
        Vector<T, RowsSize> result;
        result.get_glm_vector() = m_glm_mat * vec.get_glm_vector();
        return result;
    }

} // namespace ID

#else

#include <cstring>

namespace ID
{
    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>::~Matrix()
    {
        delete[] m_array;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>::Matrix(const Matrix& other)
    {
        memcpy(m_array, other.m_array, sizeof(T) * s_rows * s_cols);
        m_arow.m_array = m_array;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator=(const Matrix& other)
    {
        if (this != &other)
        {
            memcpy(m_array, other.m_array, sizeof(T) * s_rows * s_cols);
            m_arow.m_array = m_array;
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>::Matrix(Matrix&& other) noexcept : m_array(other.m_array)
    {
        other.m_array = nullptr;
        other.m_arow.m_array = nullptr;

        m_arow.m_array = m_array;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator=(Matrix&& other) noexcept
    {
        if (this != &other)
        {
            delete[] m_array;
            m_array = other.m_array;
            other.m_array = nullptr;

            m_arow.m_array = m_array;
            other.m_arow.m_array = nullptr;
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::get_identity() requires Math::IsDecimal<T> && Math::IsSquare<RowsSize, ColsSize>
    {
        Matrix result;
        for (std::size_t i = 0; i < s_rows; ++i)
        {
            result.element(i, i) = static_cast<T>(1);
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::get_transpose() const requires Math::IsDecimal<T>
    {
        Matrix result;
        for (std::size_t i = 0; i < s_rows; ++i)
        {
            for (std::size_t j = 0; j < s_cols; ++j)
            {
                result.element(j, i) = element(i, j);
            }
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator+(const Matrix<T, RowsSize, ColsSize>& other) const requires Math::IsDecimal<T>
    {
        Matrix result;
        for (std::size_t i = 0; i < s_rows; ++i)
        {
            for (std::size_t j = 0; j < s_cols; ++j)
            {
                result.element(i, j) = element(i, j) + other.element(i, j);
            }
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator-(const Matrix<T, RowsSize, ColsSize>& other) const requires Math::IsDecimal<T>
    {
        Matrix result;
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                result.element(i, j) = element(i, j) - other.element(i, j);
            }
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator*(const Matrix<T, RowsSize, ColsSize>& other) const requires Math::IsDecimal<T>
    {
        Matrix result;
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                T sum = static_cast<T>(0);
                for(std::size_t k = 0; k < s_cols; ++k)
                {
                    sum += element(i, k) * other.element(k, j);
                }
                result.element(i, j) = sum;
            }
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator*(const float scalar) const requires Math::IsDecimal<T>
    {
        Matrix result;
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                result.element(i, j) = element(i, j) * scalar;
            }
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize> Matrix<T, RowsSize, ColsSize>::operator/(const float scalar) const requires Math::IsDecimal<T>
    {
        Matrix result;
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                result.element(i, j) = element(i, j) / scalar;
            }
        }
        return result;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator+=(const Matrix<T, RowsSize, ColsSize>& other) requires Math::IsDecimal<T>
    {
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                element(i, j) += other.element(i, j);
            }
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator-=(const Matrix<T, RowsSize, ColsSize>& other) requires Math::IsDecimal<T>
    {
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                element(i, j) -= other.element(i, j);
            }
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator*=(const Matrix<T, RowsSize, ColsSize>& other) requires Math::IsDecimal<T>
    {
        *this = *this * other;
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator*=(const float scalar) requires Math::IsDecimal<T>
    {
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                element(i, j) *= scalar;
            }
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Matrix<T, RowsSize, ColsSize>& Matrix<T, RowsSize, ColsSize>::operator/=(const float scalar) requires Math::IsDecimal<T>
    {
        for(std::size_t i = 0; i < s_rows; ++i)
        {
            for(std::size_t j = 0; j < s_cols; ++j)
            {
                element(i, j) /= scalar;
            }
        }
        return *this;
    }

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    Vector<T, RowsSize> Matrix<T, RowsSize, ColsSize>::operator*(const Vector<T, ColsSize>& vec) const requires Math::IsDecimal<T>
    {
        Vector<T, RowsSize> result;
        for (std::size_t i = 0; i < RowsSize; ++i)
        {
            T sum = static_cast<T>(0);
            for (std::size_t j = 0; j < ColsSize; ++j)
            {
                sum += element(i, j) * vec[j];
            }
            result[i] = sum;
        }
        return result;
    }

} // namespace ID

#endif