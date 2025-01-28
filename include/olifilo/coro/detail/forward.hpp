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

struct wait_t;
struct when_all_t;
struct when_any_t;

namespace detail
{
struct promise_wait_callgraph;

template <typename T>
class promise;

template <typename... Args>
constexpr decltype(auto) forward_last(Args&&... args) noexcept
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  // Comma expression to get the last
  return (static_cast<Args&&>(args), ...);
#pragma GCC diagnostic pop
}

template <typename... Ts>
using last_type_of = std::decay_t<decltype(forward_last(std::declval<Ts>()...))>;

template <typename T, typename IdxSeq>
struct everything_with_idx;

template <typename... Ts, std::size_t... Is>
struct everything_with_idx<std::tuple<Ts...>, std::index_sequence<Is...>>
{
  using type = std::tuple<std::tuple_element_t<Is, std::tuple<Ts...>>...>;
};

template <typename... Ts>
using everything_except_last = typename everything_with_idx<std::tuple<Ts...>, std::make_index_sequence<sizeof...(Ts) - 1>>::type;

template <typename... Ts>
constexpr bool all_are_future = (is_future_v<Ts> && ...);

template <typename... Ts>
constexpr bool all_are_future<std::tuple<Ts...>> = (is_future_v<Ts> && ...);

template <typename... Ts>
constexpr bool all_are_lvalue_reference = (std::is_lvalue_reference_v<Ts> && ...);

template <typename... Ts>
constexpr bool all_are_lvalue_reference<std::tuple<Ts...>> = (std::is_lvalue_reference_v<Ts> && ...);
}  // namespace detail
}  // namespace olifilo
