#pragma once

#include <cstddef>
#include <concepts>

namespace ID::Math
{
    template<typename... T>
    concept AllSame = (std::same_as<T, std::common_type_t<T...>> && ...);

    template<typename... T>
    concept AllFloat = (std::same_as<T, float> && ...);

    template<typename... T>
    concept AllDouble = (std::same_as<T, double> && ...);

    template<typename... T>
    concept AllInt = (std::same_as<T, int> && ...);

    template<typename... T>
    concept AllBool = (std::same_as<T, bool> && ...);

    template<typename T>
    concept IsDecimal = (std::same_as<T, float> || std::same_as<T, double>);

    template<typename T, std::size_t Dim>
    concept Crossable = (IsDecimal<T> && (Dim == 3));

    template<std::size_t RowsSize, std::size_t ColsSize>
    concept IsSquare = (RowsSize == ColsSize);

#ifdef IDMATH_USE_GLM

    template<typename T, std::size_t Dim>
    concept CanBeVector = (Dim == 2 || Dim == 3 || Dim == 4) && (std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, int> || std::is_same_v<T, bool>);

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    concept CanBeMatrix = (RowsSize > 0 && ColsSize > 0) && (std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, int> || std::is_same_v<T, bool>);

#else
    template<typename T, std::size_t Dim>
    concept CanBeVector = Dim > 0;

    template<typename T, std::size_t RowsSize, std::size_t ColsSize>
    concept CanBeMatrix = RowsSize > 0 && ColsSize > 0;
#endif

}