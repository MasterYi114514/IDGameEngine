// 合法对照（backend = ID.Math.Scalar）：同一环境（同一编译器 / flags / include / 身份宏）下，
// **未改坏的输入必须编译通过**。负例判定只有在先证明这一点之后才有意义
// （benchmark skill 第 6 节第 12 条）。
//
// 本 TU 只断言"公开契约里声明存在的运算确实存在"，不测运行期行为（那是 ID.Math.Scalar Security 的事）。

#include "IDMathScalarApi.hpp"

namespace
{
    namespace scalar = ID::Math::Scalar;

    template <typename Lhs, typename Rhs>
    concept addable = requires(const Lhs& lhs, const Rhs& rhs) { lhs + rhs; };

    template <typename Lhs, typename Rhs>
    concept hadamardable = requires(const Lhs& lhs, const Rhs& rhs) { scalar::hadamard(lhs, rhs); };

    template <typename Lhs, typename Rhs>
    concept dottable = requires(const Lhs& lhs, const Rhs& rhs) { scalar::dot(lhs, rhs); };

    template <typename Value>
    concept length_taking = requires(const Value& value) { scalar::length(value); };

    template <typename Lhs, typename Rhs>
    concept crossable = requires(const Lhs& lhs, const Rhs& rhs) { scalar::cross(lhs, rhs); };

    template <typename Lhs, typename Rhs>
    concept cross2able = requires(const Lhs& lhs, const Rhs& rhs) { scalar::cross2(lhs, rhs); };

    template <typename Lhs, typename Rhs>
    concept multipliable = requires(const Lhs& lhs, const Rhs& rhs) { lhs * rhs; };

    template <typename Value>
    concept transposable = requires(const Value& value) { scalar::transpose(value); };

    template <typename Value>
    concept determinant_taking = requires(const Value& value) { scalar::determinant(value); };

    template <typename Value>
    concept inversible = requires(const Value& value, Value& out) { scalar::try_inverse(value, out); };
} // namespace

static_assert(addable<scalar::vec2, scalar::vec2>);
static_assert(addable<scalar::vec3, scalar::vec3>);
static_assert(addable<scalar::vec4, scalar::vec4>);

static_assert(hadamardable<scalar::vec3, scalar::vec3>);
static_assert(dottable<scalar::vec2, scalar::vec2>);
static_assert(dottable<scalar::vec3, scalar::vec3>);
static_assert(dottable<scalar::vec4, scalar::vec4>);

static_assert(length_taking<scalar::vec2>);
static_assert(length_taking<scalar::vec3>);
static_assert(length_taking<scalar::vec4>);

static_assert(crossable<scalar::vec3, scalar::vec3>);
static_assert(cross2able<scalar::vec2, scalar::vec2>);

static_assert(multipliable<scalar::matrix<2, 3>, scalar::matrix<3, 4>>);
static_assert(multipliable<scalar::matrix<4, 4>, scalar::matrix<4, 2>>);
static_assert(multipliable<scalar::matrix<3, 4>, scalar::vector<float, 4>>);

static_assert(transposable<scalar::matrix<2, 3>>);
static_assert(transposable<scalar::matrix<4, 2>>);

static_assert(determinant_taking<scalar::matrix<2, 2>>);
static_assert(determinant_taking<scalar::matrix<3, 3>>);
static_assert(determinant_taking<scalar::matrix<4, 4>>);

static_assert(inversible<scalar::matrix<2, 2>>);
static_assert(inversible<scalar::matrix<4, 4>>);

// identity 是显式入口（D0-03）：三种阶数都必须存在
static_assert(requires { scalar::identity<2>(); });
static_assert(requires { scalar::identity<3>(); });
static_assert(requires { scalar::identity<4>(); });
