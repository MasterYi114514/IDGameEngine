#pragma once

#include <concepts>
#include <tuple>
#include <type_traits>

#include "IDMath.hpp"

namespace ID
{
    namespace ParamConcept
    {
        template <typename... Args>
        concept AllTheSame = (sizeof...(Args) > 0) && (... && std::same_as<std::tuple_element_t<0, std::tuple<Args...>>, Args>);

        template<typename... Args>
        concept AllInt  = (... && std::same_as<int, Args>);

        template<typename... Args>
        concept AllFloat = (... && std::same_as<float, Args>);

        template<typename... Args>
        concept AllBool  = (... && std::same_as<bool, Args>);

        template<typename... Args>
        concept CanSetParam = requires
        {
            requires sizeof...(Args) > 0 && sizeof...(Args) <= 4;
            requires AllTheSame<Args...>;
            requires AllInt<Args...> || AllFloat<Args...> || AllBool<Args...>;
        };

        template<typename T>
        concept IsVec = std::same_as<T, Vec2> || std::same_as<T, Vec3> || std::same_as<T, Vec4>;

        template<typename T>
        concept IsMat = std::same_as<T, Mat2> || std::same_as<T, Mat3> || std::same_as<T, Mat4>;
    }
} // namespace ID