#pragma once

// Core
#include "Core/IDMathCore.hpp"
#include "Core/IDMathConcepts.hpp"
#include "Core/HeaderLogger.hpp"

// Vector
#include "Vector/Vector.hpp"
#include "Vector/VectorImpl.hpp"
#include "Vector/Quaternion.hpp"

// Matrix
#include "Matrix/Matrix.hpp"
#include "Matrix/MatrixImpl.hpp"

namespace ID
{
    using Pos2 = Vector<float, 2>;
    using Pos3 = Vector<float, 3>;
    
    using Vec2 = Vector<float, 2>;
    using Vec3 = Vector<float, 3>;
    using Vec4 = Vector<float, 4>;

    using Quat = Quaternion;
    
    using Mat2 = Matrix<float, 2, 2>;
    using Mat3 = Matrix<float, 3, 3>;
    using Mat4 = Matrix<float, 4, 4>;

    namespace Math
    {

        /**
         * @brief  计算两个三维向量的点积（内积）
         * @param  a  第一个向量
         * @param  b  第二个向量
         * @return 标量点积结果
         */
        inline float        dot(const Vec3& a, const Vec3& b) { return Vec3::dot(a, b); }

        /**
         * @brief  计算两个三维向量的叉积（外积）
         * @param  a  第一个向量
         * @param  b  第二个向量
         * @return 垂直于 a 和 b 所在平面的向量，方向遵循右手定则
         */
        inline Vec3         cross(const Vec3& a, const Vec3& b) { return Vec3::cross(a, b); }

        /**
         * @brief  角度制转弧度制
         * @param  degrees  角度值（单位：度）
         * @return 对应的弧度值
         * @note   本库中所有对外 API 的角度参数一律使用角度制，
         *         内部需要弧度时通过此函数转换
         */
        float IDMATH_API    radians(float degrees);


        /**
         * @brief  获取 4×4 单位矩阵
         * @return 对角线为 1、其余元素为 0 的单位矩阵
         */
        inline Mat4         get_identity_mat4() { return Mat4::get_identity(); }

        /**
         * @brief  构造观察矩阵（视图矩阵）
         * @param  eye     相机位置（世界空间坐标）
         * @param  center  目标点位置（相机看向的方向）
         * @param  up     世界空间的上方向向量（通常为 (0, 1, 0)）
         * @return 将世界空间坐标变换到相机空间的 4×4 视图矩阵
         * @note   等价于 OpenGL 的 gluLookAt / glm::lookAt
         */
        Mat4  IDMATH_API    get_look_at(const Pos3& eye, const Pos3& center, const Vec3& up);

        /**
         * @brief  构造透视投影矩阵
         * @param  fov     垂直视场角（单位：度）
         * @param  aspect  宽高比（宽度 / 高度），如 16:9 屏幕传 16.0f/9.0f
         * @param  near    近裁剪面距离（必须 > 0）
         * @param  far     远裁剪面距离（必须 > near）
         * @return 透视投影矩阵，将视锥体内的点映射到 NDC（标准化设备坐标）
         * @note   等价于 OpenGL 的 gluPerspective / glm::perspective
         */
        Mat4  IDMATH_API    get_perspective(float fov, float aspect, float near, float far);

        /**
         * @brief  构造正交投影矩阵
         * @param  left    左裁剪面 x 坐标
         * @param  right   右裁剪面 x 坐标
         * @param  bottom  下裁剪面 y 坐标
         * @param  top     上裁剪面 y 坐标
         * @param  near    近裁剪面距离
         * @param  far     远裁剪面距离
         * @return 正交投影矩阵，将长方体区域映射到 NDC
         * @note   等价于 OpenGL 的 glOrtho / glm::ortho
         */
        Mat4  IDMATH_API    get_orthographic(float left, float right,
                                float bottom, float top, float near, float far);

        /**
         * @brief  构造绕任意轴旋转的变换矩阵
         * @param  angle  旋转角度（单位：度，右手定则正方向）
         * @param  axis   旋转轴向量（无需单位化，内部会自动归一化）
         * @return 绕给定轴旋转 angle 度的 4×4 旋转矩阵
         * @note   使用 Rodrigues 旋转公式实现，等价于 glm::rotate
         */
        Mat4  IDMATH_API    get_rotation(float angle, const Vec3& axis);

        /**
         * @brief  构造平移变换矩阵
         * @param  translation  各轴的平移量（x, y, z）
         * @return 将物体沿 translation 方向平移的 4×4 矩阵
         * @note   等价于 glm::translate
         */
        Mat4  IDMATH_API    get_translation(const Vec3& translation);

        /**
         * @brief  构造缩放变换矩阵
         * @param  scale  各轴的缩放因子（x, y, z）；1.0 表示原始大小
         * @return 按 scale 各分量独立缩放的 4×4 对角矩阵
         * @note   等价于 glm::scale
         */
        Mat4  IDMATH_API    get_scale(const Vec3& scale);

        /**
         * @brief  计算 4×4 矩阵的逆矩阵
         * @param  mat  待求逆矩阵
         * @return 逆矩阵（奇异矩阵返回单位矩阵）
         * @note   等价于 glm::inverse
         */
        Mat4  IDMATH_API    get_inverse(const Mat4& mat);
    } // namespace Math
} // namespace ID