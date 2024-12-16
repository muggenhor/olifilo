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
  template <detail::timeout Timeout, typename... Ts, std::size_t... Is>
  requires(sizeof...(Ts) == sizeof...(Is))
  future<when_any_result<std::tuple<future<Ts>...>>> operator()(std::index_sequence<Is...>, Timeout timeout, future<Ts>&&... futures) const noexcept
  {
    auto& my_promise = co_await detail::current_promise();

    auto&& rv = std::move(my_promise.returned_value);
    // Take ownership of the futures *before* we first suspend to ensure they stay alive for the entire duration of this coroutine
    // Not taking them by value to ensure that allocation failure for the coroutine frame doesn't destroy them...
    rv.emplace(std::move(futures)...);

    if (const auto r = co_await wait(until::first_completed, timeout, std::get<Is>(rv->futures)...);
        !r)
      co_return {unexpect, r.error()};
    else
      rv->index = *r == sizeof...(futures) ? static_cast<std::size_t>(-1) : *r;

    co_return rv;
  }

  template <detail::timeout Timeout, typename... Ts>
  future<when_any_result<std::tuple<future<Ts>...>>> operator()(Timeout timeout, future<Ts>&&... futures) const noexcept
  {
    return (*this)(std::make_index_sequence<sizeof...(Ts)>(), timeout, std::move(futures)...);
  }

  template <typename... Ts>
  future<when_any_result<std::tuple<future<Ts>...>>> operator()(future<Ts>&&... futures) const noexcept
  {
    return (*this)(std::optional<wait_t::clock::time_point>{std::nullopt}, std::move(futures)...);
  }

  template <std::forward_iterator I, std::sentinel_for<I> S, detail::timeout Timeout = std::optional<wait_t::clock::time_point>>
  requires(is_future_v<std::iter_value_t<I>>)
  constexpr future<when_any_result<std::vector<
      std::iter_value_t<I>
  >>> operator()(I first, S const last, Timeout const timeout = {}) const noexcept
  {
    auto& my_promise = co_await detail::current_promise();

    auto&& rv = std::move(my_promise.returned_value);
    const auto count = static_cast<std::size_t>(std::distance(first, last));
    try
    {
      rv.emplace();
      rv->futures.reserve(count);
    }
    catch (const std::bad_alloc&)
    {
      co_return {unexpect, make_error_code(std::errc::not_enough_memory)};
    }

    // Take ownership of the futures *before* we first suspend to ensure they stay alive for the entire duration of this coroutine
    for (; first != last; ++first)
      rv->futures.emplace_back(std::ranges::iter_move(first));

    if (auto r = co_await wait(until::first_completed, rv->futures, timeout);
        !r)
      co_return {unexpect, r.error()};
    else
      rv->index = *r == rv->futures.end() ? static_cast<std::size_t>(-1) : *r - rv->futures.begin();

    co_return rv;
  }

  template <std::ranges::forward_range R, detail::timeout Timeout = std::optional<wait_t::clock::time_point>>
  requires(std::is_rvalue_reference_v<R&&>
        && is_future_v<std::ranges::range_value_t<R>>)
  auto operator()(R&& futures, Timeout const timeout = {}) const noexcept
  {
    using namespace std::ranges;
    return (*this)(begin(futures), end(futures), timeout);
  }
};

inline constexpr when_any_t when_any [[maybe_unused]];
}  // namespace olifilo
