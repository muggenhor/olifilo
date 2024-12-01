// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iterator>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <olifilo/expected.hpp>

#include "future.hpp"

namespace olifilo
{
struct when_all_t
{
  template <typename... Ts>
  future<std::tuple<expected<Ts>...>> operator()(future<Ts>... futures) const noexcept
  {
    ////std::string_view func_name(__PRETTY_FUNCTION__);
    ////func_name = func_name.substr(func_name.find("operator"));

    auto& my_promise = co_await my_current_promise();
    assert(my_promise.events.empty());
    assert(my_promise.root_caller == &my_promise);

    // Force promise.await_transform(await-expr) to be executed for all futures *before* suspending execution of *this* coroutine when invoking co_await.
    // Unfortunately whether the co_await pack expansion executes in this order or once per future just before suspending for each future is implementation-defined. So we need this hack...
    (my_promise.await_transform(std::move(futures)), ...);

    // Now allow this future's .get() to handle the actual I/O multiplexing
    co_return std::tuple<expected<Ts>...>((co_await futures)...);
  }

  template <std::forward_iterator I, std::sentinel_for<I> S>
  requires(is_future_v<typename std::iterator_traits<I>::value_type>)
  future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> operator()(I first, S last) const noexcept
  {
    ////std::string_view func_name(__PRETTY_FUNCTION__);
    ////func_name = func_name.substr(func_name.find("operator"));

    std::vector<typename std::iterator_traits<I>::value_type::value_type> rv;

    auto& my_promise = co_await my_current_promise();

    // Force promise.await_transform(await-expr) to be executed for all futures *before* suspending execution of *this* coroutine when invoking co_await.
    std::size_t count = 0;
    for (auto i = first; i != last; ++i, ++count)
      *i = my_promise.await_transform(std::move(*i));

    rv.reserve(count);
    // Now allow this future's .get() to handle the actual I/O multiplexing while collecting the results
    for (; first != last; ++first)
      rv.emplace_back(co_await *first);

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

inline constexpr when_all_t when_all;
}  // namespace olifilo
