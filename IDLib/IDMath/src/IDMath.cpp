#include "IDMath.hpp"

#ifdef IDMATH_USE_GLM

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ID::Math
{
    float radians(float degrees)
    {
        return degrees * 3.14159265358979323846f / 180.0f;
    }

    Mat4 get_look_at(const Pos3& eye, const Pos3& center, const Vec3& up)
    {
        Mat4 result;
        result.get_glm_mat() = glm::lookAt(eye.get_glm_vector(), center.get_glm_vector(), up.get_glm_vector());
        return result;
    }

    Mat4 get_perspective(float fov, float aspect, float near, float far)
    {
        Mat4 result;
        result.get_glm_mat() = glm::perspective(glm::radians(fov), aspect, near, far);
        return result;
    }

    Mat4 get_orthographic(float left, float right, float bottom, float top, float near, float far)
    {
        Mat4 result;
        result.get_glm_mat() = glm::ortho(left, right, bottom, top, near, far);
        return result;
    }

    Mat4 get_rotation(float angle, const Vec3& axis)
    {
        Mat4 result;
        result.get_glm_mat() = glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis.get_glm_vector());
        return result;
    }

    Mat4 get_translation(const Vec3& translation)
    {
        Mat4 result;
        result.get_glm_mat() = glm::translate(glm::mat4(1.0f), translation.get_glm_vector());
        return result;
    }

    Mat4 get_scale(const Vec3& scale)
    {
        Mat4 result;
        result.get_glm_mat() = glm::scale(glm::mat4(1.0f), scale.get_glm_vector());
        return result;
    }

} // namespace ID::Math

#else

#include <cmath>

namespace ID::Math
{
    float radians(float degrees)
    {
        return degrees * 3.14159265358979323846f / 180.0f;
    }

    Mat4 get_look_at(const Pos3& eye, const Pos3& center, const Vec3& up)
    {
        // 前向向量 (看向方向)
        Vec3 f = center - eye;
        f.normalize();

        // 右向向量
        Vec3 s = Vec3::cross(f, up);
        s.normalize();

        // 重新计算的上向向量
        Vec3 u = Vec3::cross(s, f);

        Mat4 result;
        result[0][0] = s[0];
        result[1][0] = u[0];
        result[2][0] = -f[0];
        result[3][0] = 0.0f;

        result[0][1] = s[1];
        result[1][1] = u[1];
        result[2][1] = -f[1];
        result[3][1] = 0.0f;

        result[0][2] = s[2];
        result[1][2] = u[2];
        result[2][2] = -f[2];
        result[3][2] = 0.0f;

        result[0][3] = -Vec3::dot(s, eye);
        result[1][3] = -Vec3::dot(u, eye);
        result[2][3] = Vec3::dot(f, eye);
        result[3][3] = 1.0f;

        return result;
    }

    Mat4 get_perspective(float fov, float aspect, float near, float far)
    {
        float fov_rad = radians(fov);
        float f = 1.0f / std::tan(fov_rad * 0.5f);

        Mat4 result;
        result[0][0] = f / aspect;
        result[1][0] = 0.0f;
        result[2][0] = 0.0f;
        result[3][0] = 0.0f;

        result[0][1] = 0.0f;
        result[1][1] = f;
        result[2][1] = 0.0f;
        result[3][1] = 0.0f;

        result[0][2] = 0.0f;
        result[1][2] = 0.0f;
        result[2][2] = (far + near) / (near - far);
        result[3][2] = -1.0f;

        result[0][3] = 0.0f;
        result[1][3] = 0.0f;
        result[2][3] = (2.0f * far * near) / (near - far);
        result[3][3] = 0.0f;

        return result;
    }

    Mat4 get_orthographic(float left, float right, float bottom, float top, float near, float far)
    {
        Mat4 result;
        result[0][0] = 2.0f / (right - left);
        result[1][0] = 0.0f;
        result[2][0] = 0.0f;
        result[3][0] = 0.0f;

        result[0][1] = 0.0f;
        result[1][1] = 2.0f / (top - bottom);
        result[2][1] = 0.0f;
        result[3][1] = 0.0f;

        result[0][2] = 0.0f;
        result[1][2] = 0.0f;
        result[2][2] = -2.0f / (far - near);
        result[3][2] = 0.0f;

        result[0][3] = -(right + left) / (right - left);
        result[1][3] = -(top + bottom) / (top - bottom);
        result[2][3] = -(far + near) / (far - near);
        result[3][3] = 1.0f;

        return result;
    }

    Mat4 get_rotation(float angle, const Vec3& axis)
    {
        float angle_rad = radians(angle);
        float c = std::cos(angle_rad);
        float s = std::sin(angle_rad);
        float t = 1.0f - c;

        Vec3 n = axis;
        n.normalize();

        float x = n[0], y = n[1], z = n[2];

        Mat4 result;
        result[0][0] = t * x * x + c;
        result[1][0] = t * x * y + s * z;
        result[2][0] = t * x * z - s * y;
        result[3][0] = 0.0f;

        result[0][1] = t * x * y - s * z;
        result[1][1] = t * y * y + c;
        result[2][1] = t * y * z + s * x;
        result[3][1] = 0.0f;

        result[0][2] = t * x * z + s * y;
        result[1][2] = t * y * z - s * x;
        result[2][2] = t * z * z + c;
        result[3][2] = 0.0f;

        result[0][3] = 0.0f;
        result[1][3] = 0.0f;
        result[2][3] = 0.0f;
        result[3][3] = 1.0f;

        return result;
    }

    Mat4 get_translation(const Vec3& translation)
    {
        Mat4 result = Mat4::get_identity();
        result[0][3] = translation[0];
        result[1][3] = translation[1];
        result[2][3] = translation[2];
        return result;
    }

    Mat4 get_scale(const Vec3& scale)
    {
        Mat4 result;
        result[0][0] = scale[0];
        result[1][1] = scale[1];
        result[2][2] = scale[2];
        result[3][3] = 1.0f;
        return result;
    }

} // namespace ID::Math

#endif