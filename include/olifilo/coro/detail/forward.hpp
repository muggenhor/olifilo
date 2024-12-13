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

struct when_all_t;
struct when_any_t;

namespace detail
{
struct promise_wait_callgraph;

template <typename T>
class promise;
}  // namespace detail
}  // namespace olifilo
