// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <tuple>
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
    assert((futures && ... && "not allowed to await invalid (moved-from) futures"));
    assert(((futures.handle.promise().waits_on_me == nullptr) && ... && "internal logic error: not allowed to await futures already being awaited"));

    auto& my_promise = co_await detail::current_promise();
    const auto me = std::coroutine_handle<std::remove_cvref_t<decltype(my_promise)>>::from_promise(my_promise);

    expected<when_any_result<std::tuple<future<Ts>...>>>&& rv = std::move(my_promise.returned_value);
    rv.emplace(std::move(futures)...);

    // Early escape if we discover a ready future
    (((rv->index == static_cast<std::size_t>(-1) && std::get<Is>(rv->futures).done()) ? void(rv->index = Is) : void()), ...);
    if (sizeof...(Ts) == 0 || rv->index != static_cast<std::size_t>(-1))
      co_return rv;

    // Prevent push_back() below from being able to have allocation failures
    if (auto r = my_promise.callees.reserve(sizeof...(futures), my_promise.alloc);
        !r)
      co_return {unexpect, r.error()};
    // Simulate promise.await_transform(futures)... We can't use co_await because it would wait on *all* futures.
    ((void)my_promise.callees.push_back(&std::get<Is>(rv->futures).handle.promise(), my_promise.alloc), ...);
    ((std::get<Is>(rv->futures).handle.promise().caller = &my_promise), ...);
    ((std::get<Is>(rv->futures).handle.promise().waits_on_me = me), ...);

    // Now allow this future's .get() to handle the actual I/O multiplexing
    co_await std::suspend_always();

    // Find index of the future that woke us up
    (((rv->index == static_cast<std::size_t>(-1) && std::get<Is>(rv->futures).handle.promise().waits_on_me == nullptr) ? void(rv->index = Is) : void()), ...);

    // Restore futures to being the top of their respective await call graphs with nothing waiting on them (anymore)
    ((std::get<Is>(rv->futures).handle.promise().caller = &std::get<Is>(rv->futures).handle.promise()), ...);
    ((std::get<Is>(rv->futures).handle.promise().waits_on_me = nullptr), ...);

    my_promise.callees.destroy(my_promise.alloc);

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
    const auto me = std::coroutine_handle<std::remove_cvref_t<decltype(my_promise)>>::from_promise(my_promise);

    expected<when_any_result<std::vector<typename std::iterator_traits<I>::value_type>>>&& rv = std::move(my_promise.returned_value);
    rv.emplace();
    if constexpr (std::random_access_iterator<I>)
    {
      // Prevent push_back() below from being able to have allocation failures
      if (auto r = my_promise.callees.reserve(last - first, my_promise.alloc);
          !r)
        co_return {unexpect, r.error()};
      rv->futures.reserve(last - first);
    }

    // Simulate promise.await_transform(futures)... We can't use co_await because it would wait on *all* futures.
    for (unsigned idx = 0; first != last; ++first)
    {
      assert(*first && "not allowed to await invalid (moved-from) futures");
      assert(first->handle.promise().waits_on_me == nullptr && "internal logic error: not allowed to await futures already being awaited");
      auto& future = rv->futures.emplace_back(std::move(*first));

      if (rv->index != static_cast<std::size_t>(-1))
        continue;

      // Early escape if we discover a ready future
      if (future.done())
      {
        rv->index = idx;
        // Restore futures to previous state
        while (idx)
        {
          --idx;
          auto& callee = rv->futures[idx].handle.promise();
          callee.caller = &callee;
          callee.waits_on_me = nullptr;
        }
        continue;
      }

      auto& callee = static_cast<detail::promise_wait_callgraph&>(future.handle.promise());
      assert(callee.caller == &callee && "stealing a future someone else is waiting on");
      const auto push_success = my_promise.callees.push_back(&callee, my_promise.alloc);
      [[assume(!std::random_access_iterator<I> || push_success)]];
      assert(push_success && "uhoh don't know how to handle allocation failure here!");
      callee.caller = &my_promise;
      callee.waits_on_me = me;
      ++idx;
    }

    if (rv->futures.empty() || rv->index != static_cast<std::size_t>(-1))
      co_return rv;

    // Now allow this future's .get() to handle the actual I/O multiplexing
    co_await std::suspend_always();

    unsigned idx = 0;
    for (auto& future : rv->futures)
    {
      assert(future.handle);
      auto& callee = future.handle.promise();

      // Find index of the future that woke us up
      if (rv->index == static_cast<std::size_t>(-1)
       && callee.waits_on_me == nullptr)
        rv->index = idx;
      ++idx;

      // Restore futures to being the top of their respective await call graphs with nothing waiting on them (anymore)
      callee.caller = &callee;
      callee.waits_on_me = nullptr;
    }

    my_promise.callees.destroy(my_promise.alloc);

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
