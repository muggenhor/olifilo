// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <type_traits>

namespace olifilo
{
template <typename T>
class future;

template <typename...>
struct is_future : std::false_type {};

template <typename T>
struct is_future<future<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_future_v = is_future<T>::value;

template <typename... Ts>
future<std::tuple<expected<Ts>...>> when_all(future<Ts>... futures) noexcept;
template <std::forward_iterator I, std::sentinel_for<I> S>
requires(is_future_v<typename std::iterator_traits<I>::value_type>)
future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> when_all(I first, S last) noexcept;

namespace detail
{
template <typename T>
class promise;
}  // namespace detail
}  // namespace olifilo
