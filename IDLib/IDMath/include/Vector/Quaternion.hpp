#pragma once

#ifdef IDMATH_USE_GLM
    #include <glm/glm.hpp>
    #include <glm/gtc/quaternion.hpp>
#endif

#include <cmath>

#include "Core/IDMathCore.hpp"
#include "Vector/Vector.hpp"
#include "Matrix/Matrix.hpp"

namespace ID
{

#ifdef IDMATH_USE_GLM

/*
*   四元数类，底层使用 glm::qua 实现
*     - 用于表示三维空间中的旋转
*     - 默认构造为单位四元数 (1, 0, 0, 0)
*     - 分量顺序：w（实部）、x、y、z（虚部）
*     - 采用右手定则：正角度为逆时针旋转（从轴正向看向原点）
*     - 重要成员函数：slerp（球面插值）、from_axis_angle（轴角构造）、
*       to_mat4（转 4×4 矩阵）、rotate（旋转向量）
*/
class IDMATH_API Quaternion
{
public:
    /*
    *   默认构造，初始化为单位四元数 (1, 0, 0, 0)，表示无旋转
    */
    Quaternion();

    /*
    *   直接指定四个分量构造
    *   @param w  实部（标量部分）
    *   @param x  虚部 x 分量
    *   @param y  虚部 y 分量
    *   @param z  虚部 z 分量
    */
    explicit Quaternion(float w, float x, float y, float z);

    /*
    *   从 glm::qua 构造，用于与 glm 互操作
    */
    explicit Quaternion(const glm::qua<float, glm::defaultp>& q);

    // 拷贝 & 移动（默认）
    Quaternion(const Quaternion&)            = default;
    Quaternion& operator=(const Quaternion&) = default;
    Quaternion(Quaternion&&)                 = default;
    Quaternion& operator=(Quaternion&&)      = default;

    // ---- 分量访问 ----

    float  w() const { return m_quat.w; }
    float& w()       { return m_quat.w; }
    float  x() const { return m_quat.x; }
    float& x()       { return m_quat.x; }
    float  y() const { return m_quat.y; }
    float& y()       { return m_quat.y; }
    float  z() const { return m_quat.z; }
    float& z()       { return m_quat.z; }

    float  operator[](std::size_t i) const { return m_quat[i]; }
    float& operator[](std::size_t i)       { return m_quat[i]; }

    float                  real() const { return m_quat.w; }
    Vector<float, 3>       imag() const;

    float*       data()       { return &m_quat[0]; }
    const float* data() const { return &m_quat[0]; }

    glm::qua<float, glm::defaultp>&       get_glm_quat()       { return m_quat; }
    const glm::qua<float, glm::defaultp>& get_glm_quat() const { return m_quat; }

private:
    glm::qua<float, glm::defaultp> m_quat{1.0f, 0.0f, 0.0f, 0.0f};

public:
    // ---- 静态工厂方法 ----

    static Quaternion from_axis_angle(const Vector<float, 3>& axis, float angle_deg);
    static Quaternion from_euler(float pitch, float yaw, float roll);
    static Quaternion from_rotation_matrix(const Matrix<float, 3, 3>& mat);
    static Quaternion from_rotation_matrix(const Matrix<float, 4, 4>& mat);
    static Quaternion from_look_at(const Vector<float, 3>& forward, const Vector<float, 3>& up);
    static Quaternion from_two_vectors(const Vector<float, 3>& from, const Vector<float, 3>& to);

    // ---- 四元数运算 ----

    Quaternion  operator* (const Quaternion& other) const;
    Quaternion& operator*=(const Quaternion& other);
    Vector<float, 3> operator* (const Vector<float, 3>& v) const;

    Quaternion conjugate()    const { return Quaternion(glm::conjugate(m_quat)); }
    Quaternion inverse()      const;
    float      dot(const Quaternion& other) const { return glm::dot(m_quat, other.m_quat); }

    float norm()         const { return glm::length(m_quat); }
    float norm_squared() const { return glm::dot(m_quat, m_quat); }

    void        normalize();
    Quaternion  normalized() const;
    bool        is_normalized(float epsilon = 1e-5f) const { return std::abs(norm_squared() - 1.0f) < epsilon; }
    bool        is_unit(float epsilon = 1e-5f)      const { return is_normalized(epsilon); }

    // ---- 旋转操作 ----

    Vector<float, 3> rotate(const Vector<float, 3>& v) const;
    Vector<float, 3> get_forward() const;
    Vector<float, 3> get_up()      const;
    Vector<float, 3> get_right()   const;

    // ---- 插值 ----

    static Quaternion slerp(const Quaternion& from, const Quaternion& to, float t);
    static Quaternion nlerp(const Quaternion& from, const Quaternion& to, float t);

    // ---- 转换 ----

    Matrix<float, 3, 3> to_mat3() const;
    Matrix<float, 4, 4> to_mat4() const;
    Vector<float, 3>    to_euler() const;
    void                to_axis_angle(Vector<float, 3>& axis, float& angle_deg) const;
};

#else

/*
*   四元数类，纯手动实现（不依赖 glm）
*     - 用于表示三维空间中的旋转
*     - 默认构造为单位四元数 (1, 0, 0, 0)
*     - 分量顺序：w（实部）、x、y、z（虚部）
*     - 采用右手定则：正角度为逆时针旋转（从轴正向看向原点）
*     - 重要成员函数：slerp（球面插值）、from_axis_angle（轴角构造）、
*       to_mat4（转 4×4 矩阵）、rotate（旋转向量）
*/
class IDMATH_API Quaternion
{
public:
    /*
    *   默认构造，初始化为单位四元数 (1, 0, 0, 0)，表示无旋转
    */
    Quaternion();

    /*
    *   直接指定四个分量构造
    *   @param w  实部（标量部分）
    *   @param x  虚部 x 分量
    *   @param y  虚部 y 分量
    *   @param z  虚部 z 分量
    */
    explicit Quaternion(float w, float x, float y, float z);

    // 拷贝 & 移动（默认）
    Quaternion(const Quaternion&)            = default;
    Quaternion& operator=(const Quaternion&) = default;
    Quaternion(Quaternion&&)                 = default;
    Quaternion& operator=(Quaternion&&)      = default;

    // ---- 分量访问 ----

    float  w() const { return m_w; }
    float& w()       { return m_w; }
    float  x() const { return m_x; }
    float& x()       { return m_x; }
    float  y() const { return m_y; }
    float& y()       { return m_y; }
    float  z() const { return m_z; }
    float& z()       { return m_z; }

    float  operator[](std::size_t i) const { return (&m_w)[i]; }
    float& operator[](std::size_t i)       { return (&m_w)[i]; }

    float            real() const { return m_w; }
    Vector<float, 3> imag() const { return Vector<float, 3>(m_x, m_y, m_z); }

    float*       data()       { return &m_w; }
    const float* data() const { return &m_w; }

private:
    float m_w = 1.0f;
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;

public:
    // ---- 静态工厂方法 ----

    static Quaternion from_axis_angle(const Vector<float, 3>& axis, float angle_deg);
    static Quaternion from_euler(float pitch, float yaw, float roll);
    static Quaternion from_rotation_matrix(const Matrix<float, 3, 3>& mat);
    static Quaternion from_rotation_matrix(const Matrix<float, 4, 4>& mat);
    static Quaternion from_look_at(const Vector<float, 3>& forward, const Vector<float, 3>& up);
    static Quaternion from_two_vectors(const Vector<float, 3>& from, const Vector<float, 3>& to);

    // ---- 四元数运算 ----

    Quaternion  operator* (const Quaternion& other) const;
    Quaternion& operator*=(const Quaternion& other);
    Vector<float, 3> operator* (const Vector<float, 3>& v) const;

    Quaternion conjugate()    const { return Quaternion(m_w, -m_x, -m_y, -m_z); }
    Quaternion inverse()      const;
    float      dot(const Quaternion& other) const { return m_w * other.m_w + m_x * other.m_x + m_y * other.m_y + m_z * other.m_z; }

    float norm()         const { return std::sqrt(norm_squared()); }
    float norm_squared() const { return m_w * m_w + m_x * m_x + m_y * m_y + m_z * m_z; }

    void        normalize();
    Quaternion  normalized() const;
    bool        is_normalized(float epsilon = 1e-5f) const { return std::abs(norm_squared() - 1.0f) < epsilon; }
    bool        is_unit(float epsilon = 1e-5f)      const { return is_normalized(epsilon); }

    // ---- 旋转操作 ----

    Vector<float, 3> rotate(const Vector<float, 3>& v) const;
    Vector<float, 3> get_forward() const;
    Vector<float, 3> get_up()      const;
    Vector<float, 3> get_right()   const;

    // ---- 插值 ----

    static Quaternion slerp(const Quaternion& from, const Quaternion& to, float t);
    static Quaternion nlerp(const Quaternion& from, const Quaternion& to, float t);

    // ---- 转换 ----

    Matrix<float, 3, 3> to_mat3() const;
    Matrix<float, 4, 4> to_mat4() const;
    Vector<float, 3>    to_euler() const;
    void                to_axis_angle(Vector<float, 3>& axis, float& angle_deg) const;
};

#endif

} // namespace ID
