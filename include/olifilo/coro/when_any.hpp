// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <iterator>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "future.hpp"

namespace olifilo
{
template <typename Sequence>
struct when_any_result
{
  std::size_t index = static_cast<std::size_t>(-1);
  Sequence futures;

  template <typename... Args>
  explicit constexpr when_any_result(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<Sequence, Args&&...>)
    requires(std::is_constructible_v<Sequence, Args&&...>)
    : futures(std::forward<Args>(args)...)
  {
  }
};

struct when_any_t
{
  template <typename... Ts, std::size_t... Is>
  requires(sizeof...(Ts) == sizeof...(Is))
  future<when_any_result<std::tuple<future<Ts>...>>> operator()(std::index_sequence<Is...>, future<Ts>&&... futures) const noexcept
  {
    auto& my_promise = co_await detail::current_promise();

    auto&& rv = std::move(my_promise.returned_value);
    // Take ownership of the futures *before* we first suspend to ensure they stay alive for the entire duration of this coroutine
    // Not taking them by value to ensure that allocation failure for the coroutine frame doesn't destroy them...
    rv.emplace(std::move(futures)...);

    if (const auto r = co_await wait(until::first_completed, std::get<Is>(rv->futures)...);
        !r)
      co_return {unexpect, r.error()};
    else
      rv->index = *r == sizeof...(futures) ? static_cast<std::size_t>(-1) : *r;

    co_return rv;
  }

  template <typename... Ts>
  future<when_any_result<std::tuple<future<Ts>...>>> operator()(future<Ts>&&... futures) const noexcept
  {
    return (*this)(std::make_index_sequence<sizeof...(Ts)>(), std::move(futures)...);
  }

  template <std::forward_iterator I, std::sentinel_for<I> S>
  requires(is_future_v<std::iter_value_t<I>>)
  constexpr future<when_any_result<std::vector<
      std::iter_value_t<I>
  >>> operator()(I first, S const last) const noexcept
  {
    auto& my_promise = co_await detail::current_promise();

    auto&& rv = std::move(my_promise.returned_value);
    const auto count = static_cast<std::size_t>(std::distance(first, last));
    rv.emplace();
    {
      rv->futures.reserve(count);
    }

    // Take ownership of the futures *before* we first suspend to ensure they stay alive for the entire duration of this coroutine
    for (; first != last; ++first)
      rv->futures.emplace_back(std::ranges::iter_move(first));

    if (auto r = co_await wait(until::first_completed, rv->futures);
        !r)
      co_return {unexpect, r.error()};
    else
      rv->index = *r == rv->futures.end() ? static_cast<std::size_t>(-1) : *r - rv->futures.begin();

    co_return rv;
  }

  template <std::ranges::forward_range R>
  requires(std::is_rvalue_reference_v<R&&>
        && is_future_v<std::ranges::range_value_t<R>>)
  auto operator()(R&& futures) const noexcept
  {
    using namespace std::ranges;
    return (*this)(begin(futures), end(futures));
  }
};

inline constexpr when_any_t when_any [[maybe_unused]];
}  // namespace olifilo
