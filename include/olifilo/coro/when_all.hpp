// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <iterator>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <olifilo/detail/small_vector.hpp>
#include <olifilo/expected.hpp>

#include "future.hpp"
#include "wait.hpp"

namespace olifilo
{
struct when_all_t
{
  template <typename... Ts, std::size_t... Is>
  requires(sizeof...(Ts) == sizeof...(Is))
  future<std::tuple<expected<Ts>...>> operator()(std::index_sequence<Is...>, future<Ts>&&... futures) const noexcept
  {
    // Take ownership of the futures *before* we first suspend to ensure they stay alive for the entire duration of this coroutine
    // Not taking them by value to ensure that allocation failure for the coroutine frame doesn't destroy them...
    std::tuple my_futures(std::move(futures)...);
    if (const auto r = co_await wait(until::all_completed, std::get<Is>(my_futures)...);
        !r)
      co_return {unexpect, r.error()};

    co_return {std::in_place, (co_await std::get<Is>(my_futures))...};
  }

  template <typename... Ts>
  future<std::tuple<expected<Ts>...>> operator()(future<Ts>&&... futures) const noexcept
  {
    return (*this)(std::make_index_sequence<sizeof...(Ts)>(), std::move(futures)...);
  }

  template <std::forward_iterator I, std::sentinel_for<I> S>
  requires(is_future_v<std::iter_value_t<I>>)
  future<std::vector<typename std::iter_value_t<I>::value_type>> operator()(I first, S last) const noexcept
  {
    auto& my_promise = co_await detail::current_promise();

    const auto count = std::ranges::distance(first, last);
    auto&& rv = std::move(my_promise.returned_value);
    rv.emplace();
    rv->reserve(count);

    detail::sbo_vector<std::iter_value_t<I>> my_futures;
    struct scope_exit
    {
      decltype(my_promise.alloc)& alloc_;
      decltype(my_futures)& my_futures_;
      ~scope_exit()
      {
        my_futures_.destroy(alloc_);
      }
    } scope_exit(my_promise.alloc, my_futures);
    if (auto r = my_futures.reserve(count, my_promise.alloc);
        !r)
      co_return {unexpect, r.error()};

    // Take ownership of the futures *before* we first suspend to ensure they stay alive for the entire duration of this coroutine
    for (; first != last; ++first)
      (void)my_futures.push_back(std::ranges::iter_move(first), my_promise.alloc);

    if (const auto r = co_await wait(until::all_completed, my_futures);
        !r)
      co_return {unexpect, r.error()};

    for (auto& future : my_futures)
      rv->emplace_back(co_await future);

    co_return rv;
  }

  template <std::ranges::forward_range R>
  requires(std::is_rvalue_reference_v<R&&>)
  auto operator()(R&& futures) const noexcept
  {
    using std::ranges::begin;
    using std::ranges::end;
    return (*this)(begin(futures), end(futures));
  }
};

inline constexpr when_all_t when_all [[maybe_unused]];
}  // namespace olifilo
