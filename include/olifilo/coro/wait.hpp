// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <ranges>

#include "future.hpp"
#include <olifilo/detail/small_vector.hpp>

namespace olifilo
{
enum class until
{
  all_completed,
  first_completed,
};

struct wait_t
{
  using clock = std::chrono::steady_clock;
  using duration = clock::duration;

  /**
   * @param promises list of promise-pointers representing futures to wait on
   *                 nullptr represents a future that's done().
   * @note it would be lovely if we could use the futures' coroutine_handle,
   *       and their .done() member, directly, but unfortunately conversion between coroutine_handle
   *       and promises is dependent on the alignment of the promise type (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=118014).
   *       It's also not clear that the standard even permits accessing it through bases.
   *       Though pointer-interconvertibility might be a post-condition added to coroutine_handle.
   *       When combined with expanding standard layout classes to permit non-static data members in
   *       one class per inheritance level that would effectively make
   *       std::coroutine_handle<Base>::from_promise(Derived&) defined behavior (with some
   *       restrictions)!
   * @returns the *first* index into 'promises' that represents a ready future.
   *       std::ranges::size(promises) means there is no ready future. This should only happen
   *       when std::ranges::empty(promises). Otherwise that indicates a bug in this function.
   */
  future<std::size_t>
    static operator()(
      decltype(detail::promise_wait_callgraph::callees)&        promises
    , until                                                     wait_until
    , std::optional<clock::time_point>                          timeout
    ) noexcept;

  template <typename... Ts>
  future<std::size_t>
    static constexpr operator()(
      until                            wait_until
    , std::optional<clock::time_point> timeout
    , future<Ts>&...                   futures
    ) noexcept
  {
    auto& my_promise = co_await detail::current_promise();
    decltype(my_promise.callees) promises;
    struct scope_exit
    {
      decltype(my_promise.alloc)& alloc_;
      decltype(promises)& promises_;
      ~scope_exit()
      {
        promises_.destroy(alloc_);
      }
    } scope_exit(my_promise.alloc, promises);
    {
      if (auto r = promises.reserve(sizeof...(futures) + !!timeout, my_promise.alloc);
            !r)
        co_return {unexpect, r.error()};
      (((void)promises.push_back(
           static_cast<olifilo::detail::promise_wait_callgraph*>(
             futures && !futures.done() ? &futures.handle.promise() : nullptr
           ), my_promise.alloc)), ...);
    }

    co_return co_await wait_t::operator()(promises, wait_until, timeout);
  }

  template <typename... Ts>
  future<std::size_t>
    static constexpr operator()(
      until                            wait_until
    , std::optional<duration>          timeout
    , future<Ts>&...                   futures
    ) noexcept
  {
    std::optional<clock::time_point> final_time;
    if (timeout)
      final_time = clock::now() + *timeout;
    return wait_t::operator()(wait_until, final_time, futures...);
  }

  template <typename... Ts>
  future<std::size_t>
    static constexpr operator()(
      until                            wait_until
    , future<Ts>&...                   futures
    ) noexcept
  {
    return wait_t::operator()(wait_until, std::optional<clock::time_point>{std::nullopt}, futures...);
  }

  template <std::forward_iterator I, std::sentinel_for<I> S>
  requires(is_future_v<std::iter_value_t<I>>
     && std::is_lvalue_reference_v<decltype(*std::declval<I>())>)
  future<I>
    static constexpr operator()(
      until                            wait_until
    , I                                first
    , S const                          last
    , std::optional<clock::time_point> timeout = {}
    ) noexcept
  {
    auto& my_promise = co_await detail::current_promise();
    decltype(my_promise.callees) promises;
    struct scope_exit
    {
      decltype(my_promise.alloc)& alloc_;
      decltype(promises)& promises_;
      ~scope_exit()
      {
        promises_.destroy(alloc_);
      }
    } scope_exit(my_promise.alloc, promises);
    if (auto r = promises.reserve(std::ranges::distance(first, last) + !!timeout, my_promise.alloc);
          !r)
      co_return {unexpect, r.error()};

    for (auto i = first; i != last; ++i)
      (void)promises.push_back(
          static_cast<olifilo::detail::promise_wait_callgraph*>(
            *i && !i->done() ? &i->handle.promise() : nullptr
          ), my_promise.alloc);

    if (auto r = co_await wait_t::operator()(promises, wait_until, timeout);
        !r)
      co_return {unexpect, r.error()};
    else
      co_return {std::in_place, std::next(std::move(first), *r)};
  }

  template <std::forward_iterator I, std::sentinel_for<I> S>
  requires(is_future_v<std::iter_value_t<I>>
     && std::is_lvalue_reference_v<decltype(*std::declval<I>())>)
  future<I>
    static constexpr operator()(
      until                            wait_until
    , I                                first
    , S const                          last
    , std::optional<duration>          timeout
    ) noexcept
  {
    std::optional<clock::time_point> final_time;
    if (timeout)
      final_time = clock::now() + *timeout;
    return wait_t::operator()(wait_until, first, last, final_time);
  }

  /**
   * @param wait_until  condition to wait on (all/any)
   * @param futures     set of futures to wait on
   * @param timeout     limit waiting to not exceed this time
   */
  template <std::ranges::forward_range R>
  requires(is_future_v<std::ranges::range_value_t<R>>)
  future<std::ranges::iterator_t<R>>
    static constexpr operator()(
      until                            wait_until
    , R&                               futures
    , std::optional<clock::time_point> timeout = {}
    ) noexcept
  {
    return wait_t::operator()(
        wait_until
      , std::ranges::begin(futures), std::ranges::end(futures)
      , timeout
      );
  }

  template <std::ranges::forward_range R>
  requires(is_future_v<std::ranges::range_value_t<R>>)
  future<std::ranges::iterator_t<R>>
    static constexpr operator()(
      until                            wait_until
    , R&                               futures
    , duration                         timeout
    ) noexcept
  {
    return wait_t::operator()(
        wait_until
      , std::ranges::begin(futures), std::ranges::end(futures)
      , timeout
      );
  }
};

inline constexpr wait_t wait [[maybe_unused]];
}  // namespace olifilo
