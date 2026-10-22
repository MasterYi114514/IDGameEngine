// 合法对照（backend = ID.Math.SIMD）：同一环境（同一编译器 / flags / include / 身份宏）下，
// **未改坏的输入必须编译通过**。负例判定只有在先证明这一点之后才有意义
// （benchmark skill 第 6 节第 12 条）。
//
// 本 TU 只断言"公开契约里声明存在的运算确实存在"，不测运行期行为（那是 ID.Math.SIMD Security 的事）。

#include "IDMathSIMDApi.hpp"

namespace
{
    namespace simd = ID::Math::SIMD;

    template <typename Lhs, typename Rhs>
    concept addable = requires(const Lhs& lhs, const Rhs& rhs) { lhs + rhs; };

    template <typename Lhs, typename Rhs>
    concept hadamardable = requires(const Lhs& lhs, const Rhs& rhs) { simd::hadamard(lhs, rhs); };

    template <typename Lhs, typename Rhs>
    concept dottable = requires(const Lhs& lhs, const Rhs& rhs) { simd::dot(lhs, rhs); };

    template <typename Value>
    concept length_taking = requires(const Value& value) { simd::length(value); };

    template <typename Lhs, typename Rhs>
    concept crossable = requires(const Lhs& lhs, const Rhs& rhs) { simd::cross(lhs, rhs); };

    template <typename Lhs, typename Rhs>
    concept cross2able = requires(const Lhs& lhs, const Rhs& rhs) { simd::cross2(lhs, rhs); };

    template <typename Lhs, typename Rhs>
    concept multipliable = requires(const Lhs& lhs, const Rhs& rhs) { lhs * rhs; };

    template <typename Value>
    concept transposable = requires(const Value& value) { simd::transpose(value); };

    template <typename Value>
    concept determinant_taking = requires(const Value& value) { simd::determinant(value); };

    template <typename Value>
    concept inversible = requires(const Value& value, Value& out) { simd::try_inverse(value, out); };

    template <typename Vector, typename Component>
    concept packable = requires(const Vector& value, Component* out) { simd::pack(value, out); };

    template <typename Vector, typename Component>
    concept unpackable = requires(const Component* source, Vector& out) { simd::unpack(source, out); };
} // namespace

static_assert(addable<simd::vec2, simd::vec2>);
static_assert(addable<simd::vec3, simd::vec3>);
static_assert(addable<simd::vec4, simd::vec4>);

static_assert(hadamardable<simd::vec3, simd::vec3>);
static_assert(dottable<simd::vec2, simd::vec2>);
static_assert(dottable<simd::vec3, simd::vec3>);
static_assert(dottable<simd::vec4, simd::vec4>);

static_assert(length_taking<simd::vec2>);
static_assert(length_taking<simd::vec3>);
static_assert(length_taking<simd::vec4>);

static_assert(crossable<simd::vec3, simd::vec3>);
static_assert(cross2able<simd::vec2, simd::vec2>);

static_assert(multipliable<simd::matrix<2, 3>, simd::matrix<3, 4>>);
static_assert(multipliable<simd::matrix<4, 4>, simd::matrix<4, 2>>);
static_assert(multipliable<simd::matrix<3, 4>, simd::vector<float, 4>>);

static_assert(transposable<simd::matrix<2, 3>>);
static_assert(transposable<simd::matrix<4, 2>>);

static_assert(determinant_taking<simd::matrix<2, 2>>);
static_assert(determinant_taking<simd::matrix<3, 3>>);
static_assert(determinant_taking<simd::matrix<4, 4>>);

static_assert(inversible<simd::matrix<2, 2>>);
static_assert(inversible<simd::matrix<4, 4>>);

// identity 是显式静态入口（SIMD 侧是 matrix<N,N>::identity()）
static_assert(requires { simd::matrix<2, 2>::identity(); });
static_assert(requires { simd::matrix<4, 4>::identity(); });

// pack / unpack 是 SIMD 侧独有的交换格式入口
static_assert(packable<simd::vec3, float>);
static_assert(unpackable<simd::vec3, float>);
static_assert(requires(const simd::matrix<3, 4>& value, float* out) { simd::pack(value, out); });
static_assert(requires(const float* source, simd::matrix<3, 4>& out) { simd::unpack(source, out); });
