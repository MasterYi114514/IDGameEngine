#pragma once

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
    *   基于 glm 的矩阵封装
    *     - 底层使用 glm::mat 存储
    *     - 按照列优先的方式存储矩阵元素
    *     - 提供与非 glm 版本一致的接口
    */
    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    class IDMATH_API Matrix
    {
    public:
        Matrix() { m_arow.m_array = m_data; }
        ~Matrix() = default;

        // 拷贝
        Matrix(const Matrix& other);
        Matrix& operator=(const Matrix&);

        // 移动
        Matrix(Matrix&&) noexcept;
        Matrix& operator=(Matrix&&) noexcept;

    private:
        static constexpr std::size_t s_rows = RowsSize;
        static constexpr std::size_t s_cols = ColsSize;

        // 对用 [] 访问的支持
        template<std::size_t RowStride>
        struct AbstractRow
        {
            mutable std::size_t m_row_index;
            T*          m_array;

            T& operator[](std::size_t col_index)
            {
                return m_array[col_index * RowStride + m_row_index];
            }

            const T& operator[](std::size_t col_index) const
            {
                return m_array[col_index * RowStride + m_row_index];
            }
        };

        AbstractRow<s_rows> m_arow;

    public:
        AbstractRow<s_rows>& operator[](std::size_t row_index)
        {
            m_arow.m_row_index = row_index;
            return m_arow;
        }

        const AbstractRow<s_rows>& operator[](std::size_t row_index) const
        {
            m_arow.m_row_index = row_index;
            return m_arow;
        }

        T& element(std::size_t row, std::size_t col) { return m_data[col * s_rows + row]; }
        const T& element(std::size_t row, std::size_t col) const { return m_data[col * s_rows + row]; }

    private:
        glm::mat<ColsSize, RowsSize, T> m_glm_mat{};
        T* m_data = &m_glm_mat[0][0];

    public:
        // 矩阵操作，仅支持 T 是 float 或 double 的情况

        static Matrix get_identity() requires Math::IsDecimal<T> && Math::IsSquare<RowsSize, ColsSize>;
        Matrix get_transpose() const requires Math::IsDecimal<T>;

        Matrix  operator+ (const Matrix& other)     const   requires Math::IsDecimal<T>;
        Matrix  operator- (const Matrix& other)     const   requires Math::IsDecimal<T>;
        Matrix  operator* (const Matrix& other)     const   requires Math::IsDecimal<T>;
        Matrix  operator* (const float   scalar)    const   requires Math::IsDecimal<T>;
        Matrix  operator/ (const float   scalar)    const   requires Math::IsDecimal<T>;
        Matrix& operator+=(const Matrix& other)             requires Math::IsDecimal<T>;
        Matrix& operator-=(const Matrix& other)             requires Math::IsDecimal<T>;
        Matrix& operator*=(const Matrix& other)             requires Math::IsDecimal<T>;
        Matrix& operator*=(const float   scalar)            requires Math::IsDecimal<T>;
        Matrix& operator/=(const float   scalar)            requires Math::IsDecimal<T>;

        Vector<T, RowsSize> operator*(const Vector<T, ColsSize>& vec) const requires Math::IsDecimal<T>;

    public:
        // OpenGL 所需接口
        T* get_data() { return m_data; }
        const T* get_data() const { return m_data; }

        glm::mat<ColsSize, RowsSize, T>& get_glm_mat() { return m_glm_mat; }
        const glm::mat<ColsSize, RowsSize, T>& get_glm_mat() const { return m_glm_mat; }
    };

#else
    /*
    *   底层是一位数组实现的矩阵类
    *     - 允许任意行列数的矩阵类型
    *     - 按照列优先的方式存储矩阵元素
    */
    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    requires Math::CanBeMatrix<T, RowsSize, ColsSize>
    class IDMATH_API Matrix
    {
    public:
        Matrix() { m_arow.m_array = m_array; }
        ~Matrix();

        // 拷贝
        Matrix(const Matrix& other);
        Matrix& operator=(const Matrix&);

        // 移动
        Matrix(Matrix&&) noexcept;
        Matrix& operator=(Matrix&&) noexcept;

    private:
        // 对用 [] 访问的支持
        template<std::size_t RowStride>
        struct AbstractRow
        {
            mutable std::size_t m_row_index;
            T*          m_array;

            T& operator[](std::size_t col_index)
            {
                return m_array[col_index * RowStride + m_row_index];
            }

            const T& operator[](std::size_t col_index) const
            {
                return m_array[col_index * RowStride + m_row_index];
            }
        };

        AbstractRow<RowsSize> m_arow;

    public:
        AbstractRow<RowsSize>& operator[](std::size_t row_index)
        {
            m_arow.m_row_index = row_index;
            return m_arow;
        }

        const AbstractRow<RowsSize>& operator[](std::size_t row_index) const
        {
            m_arow.m_row_index = row_index;
            return m_arow;
        }

        T& element(std::size_t row, std::size_t col) { return m_array[col * s_rows + row]; }
        const T& element(std::size_t row, std::size_t col) const { return m_array[col * s_rows + row]; }

    private:
        static constexpr std::size_t s_rows = RowsSize;
        static constexpr std::size_t s_cols = ColsSize;

        T* m_array = new T[s_rows * s_cols]();

    public:
        // 矩阵操作，仅支持 T 是 float 或 double 的情况

        static Matrix get_identity() requires Math::IsDecimal<T> && Math::IsSquare<RowsSize, ColsSize>;
        Matrix get_transpose() const requires Math::IsDecimal<T>;

        Matrix  operator+ (const Matrix& other)     const   requires Math::IsDecimal<T>;
        Matrix  operator- (const Matrix& other)     const   requires Math::IsDecimal<T>;
        Matrix  operator* (const Matrix& other)     const   requires Math::IsDecimal<T>;
        Matrix  operator* (const float   scalar)    const   requires Math::IsDecimal<T>;
        Matrix  operator/ (const float   scalar)    const   requires Math::IsDecimal<T>;
        Matrix& operator+=(const Matrix& other)             requires Math::IsDecimal<T>;
        Matrix& operator-=(const Matrix& other)             requires Math::IsDecimal<T>;
        Matrix& operator*=(const Matrix& other)             requires Math::IsDecimal<T>;
        Matrix& operator*=(const float   scalar)            requires Math::IsDecimal<T>;
        Matrix& operator/=(const float   scalar)            requires Math::IsDecimal<T>;

        Vector<T, RowsSize> operator*(const Vector<T, ColsSize>& vec) const requires Math::IsDecimal<T>;

    public:
        // OpenGL 所需接口
        T* get_data() { return m_array; }
        const T* get_data() const { return m_array; }
    };

#endif
} // namespace ID