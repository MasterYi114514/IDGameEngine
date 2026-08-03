#include "Vector/Quaternion.hpp"

#ifdef IDMATH_USE_GLM
    #include <glm/gtc/quaternion.hpp>
#endif

#include <algorithm>
#include <cmath>

namespace ID
{

// ============================================================================
//  GLM 分支实现
// ============================================================================
#ifdef IDMATH_USE_GLM

Quaternion::Quaternion() = default;

Quaternion::Quaternion(float w, float x, float y, float z)
    : m_quat(w, x, y, z)
{
}

Quaternion::Quaternion(const glm::qua<float, glm::defaultp>& q)
    : m_quat(q)
{
}

Vector<float, 3> Quaternion::imag() const
{
    return Vector<float, 3>(m_quat.x, m_quat.y, m_quat.z);
}

// ---- 静态工厂方法 ----

Quaternion Quaternion::from_axis_angle(const Vector<float, 3>& axis, float angle_deg)
{
    float angle_rad = glm::radians(angle_deg);
    return Quaternion(glm::angleAxis(angle_rad, axis.get_glm_vector()));
}

Quaternion Quaternion::from_euler(float pitch, float yaw, float roll)
{
    glm::vec<3, float, glm::defaultp> euler_rad(
        glm::radians(pitch),
        glm::radians(yaw),
        glm::radians(roll)
    );
    return Quaternion(glm::qua<float, glm::defaultp>(euler_rad));
}

Quaternion Quaternion::from_rotation_matrix(const Matrix<float, 3, 3>& mat)
{
    return Quaternion(glm::quat_cast(mat.get_glm_mat()));
}

Quaternion Quaternion::from_rotation_matrix(const Matrix<float, 4, 4>& mat)
{
    return Quaternion(glm::quat_cast(mat.get_glm_mat()));
}

Quaternion Quaternion::from_look_at(const Vector<float, 3>& forward, const Vector<float, 3>& up)
{
    return Quaternion(glm::quatLookAtRH(forward.get_glm_vector(), up.get_glm_vector()));
}

Quaternion Quaternion::from_two_vectors(const Vector<float, 3>& from, const Vector<float, 3>& to)
{
    Vector<float, 3> f = from;
    f.normalize();
    Vector<float, 3> t = to;
    t.normalize();

    float d = Vector<float, 3>::dot(f, t);

    // 两向量几乎平行，无需旋转
    if(d > 0.9999f)
        return Quaternion();

    // 两向量几乎反向，绕任意正交轴旋转 180°
    if(d < -0.9999f)
    {
        Vector<float, 3> ortho(1.0f, 0.0f, 0.0f);
        if(std::abs(f[0]) > 0.999f)
            ortho = Vector<float, 3>(0.0f, 1.0f, 0.0f);
        Vector<float, 3> axis = Vector<float, 3>::cross(f, ortho);
        axis.normalize();
        return from_axis_angle(axis, 180.0f);
    }

    Vector<float, 3> axis = Vector<float, 3>::cross(f, t);
    axis.normalize();
    float angle_rad = std::acos(d);
    float angle_deg = glm::degrees(angle_rad);
    return from_axis_angle(axis, angle_deg);
}

// ---- 四元数运算 ----

Quaternion Quaternion::operator*(const Quaternion& other) const
{
    return Quaternion(m_quat * other.m_quat);
}

Quaternion& Quaternion::operator*=(const Quaternion& other)
{
    m_quat *= other.m_quat;
    return *this;
}

Vector<float, 3> Quaternion::operator*(const Vector<float, 3>& v) const
{
    glm::vec<3, float, glm::defaultp> result = m_quat * v.get_glm_vector();
    return Vector<float, 3>(result.x, result.y, result.z);
}

Quaternion Quaternion::inverse() const
{
    return Quaternion(glm::inverse(m_quat));
}

void Quaternion::normalize()
{
    m_quat = glm::normalize(m_quat);
}

Quaternion Quaternion::normalized() const
{
    return Quaternion(glm::normalize(m_quat));
}

// ---- 旋转操作 ----

Vector<float, 3> Quaternion::rotate(const Vector<float, 3>& v) const
{
    return (*this) * v;
}

Vector<float, 3> Quaternion::get_forward() const
{
    return rotate(Vector<float, 3>(0.0f, 0.0f, -1.0f));
}

Vector<float, 3> Quaternion::get_up() const
{
    return rotate(Vector<float, 3>(0.0f, 1.0f, 0.0f));
}

Vector<float, 3> Quaternion::get_right() const
{
    return rotate(Vector<float, 3>(1.0f, 0.0f, 0.0f));
}

// ---- 插值 ----

Quaternion Quaternion::slerp(const Quaternion& from, const Quaternion& to, float t)
{
    return Quaternion(glm::slerp(from.m_quat, to.m_quat, t));
}

Quaternion Quaternion::nlerp(const Quaternion& from, const Quaternion& to, float t)
{
    // 走最短路径
    glm::qua<float, glm::defaultp> q_to = to.m_quat;
    if(glm::dot(from.m_quat, q_to) < 0.0f)
        q_to = -q_to;
    glm::qua<float, glm::defaultp> result = glm::lerp(from.m_quat, q_to, t);
    return Quaternion(glm::normalize(result));
}

// ---- 转换 ----

Matrix<float, 3, 3> Quaternion::to_mat3() const
{
    glm::mat<3, 3, float, glm::defaultp> glm_mat = glm::mat3_cast(m_quat);
    Matrix<float, 3, 3> result;
    for(std::size_t r = 0; r < 3; ++r)
        for(std::size_t c = 0; c < 3; ++c)
            result[r][c] = glm_mat[c][r];
    return result;
}

Matrix<float, 4, 4> Quaternion::to_mat4() const
{
    glm::mat<4, 4, float, glm::defaultp> glm_mat = glm::mat4_cast(m_quat);
    Matrix<float, 4, 4> result;
    for(std::size_t r = 0; r < 4; ++r)
        for(std::size_t c = 0; c < 4; ++c)
            result[r][c] = glm_mat[c][r];
    return result;
}

Vector<float, 3> Quaternion::to_euler() const
{
    glm::vec<3, float, glm::defaultp> euler_rad = glm::eulerAngles(m_quat);
    return Vector<float, 3>(
        glm::degrees(euler_rad.x),
        glm::degrees(euler_rad.y),
        glm::degrees(euler_rad.z)
    );
}

void Quaternion::to_axis_angle(Vector<float, 3>& axis, float& angle_deg) const
{
    float angle_rad = glm::angle(m_quat);
    angle_deg = glm::degrees(angle_rad);
    glm::vec<3, float, glm::defaultp> glm_axis = glm::axis(m_quat);
    axis = Vector<float, 3>(glm_axis.x, glm_axis.y, glm_axis.z);
}

// ============================================================================
//  非 GLM 分支实现
// ============================================================================
#else

Quaternion::Quaternion() = default;

Quaternion::Quaternion(float w, float x, float y, float z)
    : m_w(w), m_x(x), m_y(y), m_z(z)
{
}

// ---- 静态工厂方法 ----

Quaternion Quaternion::from_axis_angle(const Vector<float, 3>& axis, float angle_deg)
{
    float angle_rad = angle_deg * (3.14159265358979323846f / 180.0f);
    float half_angle = angle_rad * 0.5f;
    float s = std::sin(half_angle);
    float c = std::cos(half_angle);

    Vector<float, 3> a = axis;
    a.normalize();

    Quaternion result(c, a[0] * s, a[1] * s, a[2] * s);
    result.normalize();
    return result;
}

Quaternion Quaternion::from_euler(float pitch, float yaw, float roll)
{
    float p = pitch * (3.14159265358979323846f / 180.0f) * 0.5f;
    float y = yaw   * (3.14159265358979323846f / 180.0f) * 0.5f;
    float r = roll  * (3.14159265358979323846f / 180.0f) * 0.5f;

    float cp = std::cos(p), sp = std::sin(p);
    float cy = std::cos(y), sy = std::sin(y);
    float cr = std::cos(r), sr = std::sin(r);

    // Tait-Bryan ZYX，与 glm::qua(vec3(pitch,yaw,roll)) 一致
    float w = cp * cy * cr + sp * sy * sr;
    float x = sp * cy * cr - cp * sy * sr;
    float y_ = cp * sy * cr + sp * cy * sr;
    float z = cp * cy * sr - sp * sy * cr;

    return Quaternion(w, x, y_, z);
}

Quaternion Quaternion::from_rotation_matrix(const Matrix<float, 3, 3>& mat)
{
    float r00 = mat[0][0], r01 = mat[0][1], r02 = mat[0][2];
    float r10 = mat[1][0], r11 = mat[1][1], r12 = mat[1][2];
    float r20 = mat[2][0], r21 = mat[2][1], r22 = mat[2][2];

    float trace = r00 + r11 + r22;
    float w, x, y, z;

    if(trace > 0.0f)
    {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        w = s * 0.25f;
        x = (r21 - r12) / s;
        y = (r02 - r20) / s;
        z = (r10 - r01) / s;
    }
    else if(r00 > r11 && r00 > r22)
    {
        float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        w = (r21 - r12) / s;
        x = s * 0.25f;
        y = (r01 + r10) / s;
        z = (r02 + r20) / s;
    }
    else if(r11 > r22)
    {
        float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        w = (r02 - r20) / s;
        x = (r01 + r10) / s;
        y = s * 0.25f;
        z = (r12 + r21) / s;
    }
    else
    {
        float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        w = (r10 - r01) / s;
        x = (r02 + r20) / s;
        y = (r12 + r21) / s;
        z = s * 0.25f;
    }

    return Quaternion(w, x, y, z);
}

Quaternion Quaternion::from_rotation_matrix(const Matrix<float, 4, 4>& mat)
{
    // 提取 3×3 旋转子矩阵
    Matrix<float, 3, 3> mat3;
    for(std::size_t r = 0; r < 3; ++r)
        for(std::size_t c = 0; c < 3; ++c)
            mat3[r][c] = mat[r][c];
    return from_rotation_matrix(mat3);
}

Quaternion Quaternion::from_look_at(const Vector<float, 3>& forward, const Vector<float, 3>& up)
{
    Vector<float, 3> f = forward;
    f.normalize();

    Vector<float, 3> r = Vector<float, 3>::cross(up, f);
    r.normalize();

    Vector<float, 3> u = Vector<float, 3>::cross(f, r);

    // 旋转矩阵列向量：right, corrected_up, -forward
    Matrix<float, 3, 3> mat;
    mat[0][0] = r[0]; mat[0][1] = u[0]; mat[0][2] = -f[0];
    mat[1][0] = r[1]; mat[1][1] = u[1]; mat[1][2] = -f[1];
    mat[2][0] = r[2]; mat[2][1] = u[2]; mat[2][2] = -f[2];

    return from_rotation_matrix(mat);
}

Quaternion Quaternion::from_two_vectors(const Vector<float, 3>& from, const Vector<float, 3>& to)
{
    Vector<float, 3> f = from;
    f.normalize();
    Vector<float, 3> t = to;
    t.normalize();

    float d = Vector<float, 3>::dot(f, t);

    // 两向量几乎平行，无需旋转
    if(d > 0.9999f)
        return Quaternion();

    // 两向量几乎反向，绕任意正交轴旋转 180°
    if(d < -0.9999f)
    {
        Vector<float, 3> ortho(1.0f, 0.0f, 0.0f);
        if(std::abs(f[0]) > 0.999f)
            ortho = Vector<float, 3>(0.0f, 1.0f, 0.0f);
        Vector<float, 3> axis = Vector<float, 3>::cross(f, ortho);
        axis.normalize();
        return from_axis_angle(axis, 180.0f);
    }

    Vector<float, 3> axis = Vector<float, 3>::cross(f, t);
    axis.normalize();
    float angle_rad = std::acos(d);
    float angle_deg = angle_rad * (180.0f / 3.14159265358979323846f);
    return from_axis_angle(axis, angle_deg);
}

// ---- 四元数运算 ----

Quaternion Quaternion::operator*(const Quaternion& other) const
{
    return Quaternion(
        m_w * other.m_w - m_x * other.m_x - m_y * other.m_y - m_z * other.m_z,
        m_w * other.m_x + m_x * other.m_w + m_y * other.m_z - m_z * other.m_y,
        m_w * other.m_y - m_x * other.m_z + m_y * other.m_w + m_z * other.m_x,
        m_w * other.m_z + m_x * other.m_y - m_y * other.m_x + m_z * other.m_w
    );
}

Quaternion& Quaternion::operator*=(const Quaternion& other)
{
    *this = (*this) * other;
    return *this;
}

Vector<float, 3> Quaternion::operator*(const Vector<float, 3>& v) const
{
    Vector<float, 3> qv(m_x, m_y, m_z);
    Vector<float, 3> uv = Vector<float, 3>::cross(qv, v);
    Vector<float, 3> uuv = Vector<float, 3>::cross(qv, uv);
    return v + (uv * m_w + uuv) * 2.0f;
}

Quaternion Quaternion::inverse() const
{
    float ns = norm_squared();
    float inv_ns = 1.0f / ns;
    return Quaternion(m_w * inv_ns, -m_x * inv_ns, -m_y * inv_ns, -m_z * inv_ns);
}

void Quaternion::normalize()
{
    float n = norm();
    if(n > 1e-8f)
    {
        float inv_n = 1.0f / n;
        m_w *= inv_n;
        m_x *= inv_n;
        m_y *= inv_n;
        m_z *= inv_n;
    }
}

Quaternion Quaternion::normalized() const
{
    Quaternion result(*this);
    result.normalize();
    return result;
}

// ---- 旋转操作 ----

Vector<float, 3> Quaternion::rotate(const Vector<float, 3>& v) const
{
    return (*this) * v;
}

Vector<float, 3> Quaternion::get_forward() const
{
    return rotate(Vector<float, 3>(0.0f, 0.0f, -1.0f));
}

Vector<float, 3> Quaternion::get_up() const
{
    return rotate(Vector<float, 3>(0.0f, 1.0f, 0.0f));
}

Vector<float, 3> Quaternion::get_right() const
{
    return rotate(Vector<float, 3>(1.0f, 0.0f, 0.0f));
}

// ---- 插值 ----

Quaternion Quaternion::slerp(const Quaternion& from, const Quaternion& to, float t)
{
    float cos_theta = from.dot(to);

    // 走最短路径
    Quaternion q_to = to;
    if(cos_theta < 0.0f)
    {
        q_to = Quaternion(-to.m_w, -to.m_x, -to.m_y, -to.m_z);
        cos_theta = -cos_theta;
    }

    // 角度很小，退化为 nlerp
    if(cos_theta > 0.9995f)
        return nlerp(from, q_to, t);

    float theta = std::acos(cos_theta);
    float sin_theta = std::sin(theta);
    float a = std::sin((1.0f - t) * theta) / sin_theta;
    float b = std::sin(t * theta) / sin_theta;

    return Quaternion(
        a * from.m_w + b * q_to.m_w,
        a * from.m_x + b * q_to.m_x,
        a * from.m_y + b * q_to.m_y,
        a * from.m_z + b * q_to.m_z
    );
}

Quaternion Quaternion::nlerp(const Quaternion& from, const Quaternion& to, float t)
{
    float cos_theta = from.dot(to);

    Quaternion q_to = to;
    if(cos_theta < 0.0f)
        q_to = Quaternion(-to.m_w, -to.m_x, -to.m_y, -to.m_z);

    Quaternion result(
        from.m_w + (q_to.m_w - from.m_w) * t,
        from.m_x + (q_to.m_x - from.m_x) * t,
        from.m_y + (q_to.m_y - from.m_y) * t,
        from.m_z + (q_to.m_z - from.m_z) * t
    );
    result.normalize();
    return result;
}

// ---- 转换 ----

Matrix<float, 3, 3> Quaternion::to_mat3() const
{
    float xx = m_x * m_x, yy = m_y * m_y, zz = m_z * m_z;
    float xy = m_x * m_y, xz = m_x * m_z, yz = m_y * m_z;
    float wx = m_w * m_x, wy = m_w * m_y, wz = m_w * m_z;

    Matrix<float, 3, 3> result;
    result[0][0] = 1.0f - 2.0f * (yy + zz);
    result[0][1] = 2.0f * (xy - wz);
    result[0][2] = 2.0f * (xz + wy);
    result[1][0] = 2.0f * (xy + wz);
    result[1][1] = 1.0f - 2.0f * (xx + zz);
    result[1][2] = 2.0f * (yz - wx);
    result[2][0] = 2.0f * (xz - wy);
    result[2][1] = 2.0f * (yz + wx);
    result[2][2] = 1.0f - 2.0f * (xx + yy);
    return result;
}

Matrix<float, 4, 4> Quaternion::to_mat4() const
{
    Matrix<float, 3, 3> mat3 = to_mat3();
    Matrix<float, 4, 4> result;
    for(std::size_t r = 0; r < 3; ++r)
        for(std::size_t c = 0; c < 3; ++c)
            result[r][c] = mat3[r][c];
    result[3][3] = 1.0f;
    return result;
}

Vector<float, 3> Quaternion::to_euler() const
{
    // Tait-Bryan ZYX，与 glm::eulerAngles 一致
    float sin_pitch = -2.0f * (m_x * m_z - m_w * m_y);
    sin_pitch = std::clamp(sin_pitch, -1.0f, 1.0f);
    float pitch = std::asin(sin_pitch);

    float yaw   = std::atan2(2.0f * (m_x * m_y + m_w * m_z), 1.0f - 2.0f * (m_y * m_y + m_z * m_z));
    float roll  = std::atan2(2.0f * (m_y * m_z + m_w * m_x), 1.0f - 2.0f * (m_x * m_x + m_y * m_y));

    constexpr float rad_to_deg = 180.0f / 3.14159265358979323846f;
    return Vector<float, 3>(pitch * rad_to_deg, yaw * rad_to_deg, roll * rad_to_deg);
}

void Quaternion::to_axis_angle(Vector<float, 3>& axis, float& angle_deg) const
{
    float ns = m_x * m_x + m_y * m_y + m_z * m_z;
    float angle_rad;

    if(ns < 1e-12f)
    {
        // 单位四元数附近，任意轴，角度为 0
        axis = Vector<float, 3>(1.0f, 0.0f, 0.0f);
        angle_deg = 0.0f;
        return;
    }

    angle_rad = 2.0f * std::atan2(std::sqrt(ns), m_w);
    constexpr float rad_to_deg = 180.0f / 3.14159265358979323846f;
    angle_deg = angle_rad * rad_to_deg;

    float inv_sin_half = 1.0f / std::sqrt(ns);
    axis = Vector<float, 3>(m_x * inv_sin_half, m_y * inv_sin_half, m_z * inv_sin_half);
}

#endif

} // namespace ID
