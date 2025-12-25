// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <type_traits>

#include "future.hpp"
#include <olifilo/detail/small_vector.hpp>

namespace olifilo
{
namespace detail
{
using wait_clock = std::chrono::steady_clock;

template <typename T>
constexpr bool is_time_point = false;

template <typename Duration>
constexpr bool is_time_point<std::chrono::time_point<wait_clock, Duration>> = true;

template <typename T>
constexpr bool is_time_point<std::optional<T>> = is_time_point<T>;

template <typename T>
constexpr bool is_duration = is_time_point<T>;

template <typename Rep, typename Period>
constexpr bool is_duration<std::chrono::duration<Rep, Period>> = true;

template <typename T>
constexpr bool is_duration<std::optional<T>> = is_duration<T>;

template <typename T>
constexpr bool is_timeout = is_time_point<T> || is_duration<T>;

template <typename T>
concept timeout = detail::is_timeout<T>;

template <timeout Timeout>
constexpr std::optional<wait_clock::time_point> to_timeout_point(Timeout timeout) noexcept
{
  if constexpr (detail::is_time_point<Timeout>)
  {
    return timeout;
  }
  else
  {
    if constexpr (std::constructible_from<bool, Timeout>)
    {
      if (timeout)
        return wait_clock::now() + *timeout;
    }
    else
    {
      return wait_clock::now() + timeout;
    }
  }
}
};

enum class until
{
  all_completed,
  first_completed,
};

struct wait_t
{
  using clock = detail::wait_clock;
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
  requires(detail::timeout<std::decay_t<detail::last_type_of<Ts...>>>
       && detail::all_are_future<detail::everything_except_last<std::remove_reference_t<Ts>...>>
       && detail::all_are_lvalue_reference<detail::everything_except_last<Ts...>>)
  future<std::size_t>
    static constexpr operator()(
      until                            wait_until
    , Ts&&...                          futures_with_timeout_at_end
    ) noexcept
  {
    auto my_timeout = detail::to_timeout_point(detail::forward_last(futures_with_timeout_at_end...));
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
      if (auto r = promises.reserve(sizeof...(futures_with_timeout_at_end) - (my_timeout ? 0 : 1), my_promise.alloc);
            !r)
        co_return {unexpect, r.error()};
      [&promises, &my_promise]<typename... NTs, std::size_t... NIs>(
            std::index_sequence<NIs...>
          , std::tuple<NTs&...> futures) noexcept
        {
          // push every parameter from the tuple for which we have an index
          (((void)promises.push_back(
               static_cast<olifilo::detail::promise_wait_callgraph*>(
                 std::get<NIs>(futures) && !std::get<NIs>(futures).done() ? &std::get<NIs>(futures).handle.promise() : nullptr
               ), my_promise.alloc)), ...);
        }(
          // ensure the last parameter in the pack doesn't have an index for it
          std::make_index_sequence<sizeof...(Ts) - 1>()
        , std::forward_as_tuple(futures_with_timeout_at_end...)
        );
    }

    co_return co_await wait_t::operator()(promises, wait_until, my_timeout);
  }

  template <typename... Ts>
  future<std::size_t>
    static constexpr operator()(
      until                            wait_until
    , future<Ts>&...                   futures
    ) noexcept
  {
    return wait_t::operator()(wait_until, futures..., std::optional<clock::time_point>{std::nullopt});
  }

  template <std::forward_iterator I, std::sentinel_for<I> S, detail::timeout Timeout = std::optional<clock::time_point>>
  requires(is_future_v<std::iter_value_t<I>>
     && std::is_lvalue_reference_v<decltype(*std::declval<I>())>)
  future<I>
    static constexpr operator()(
      until                            wait_until
    , I                                first
    , S const                          last
    , Timeout                          timeout = {}
    ) noexcept
  {
    auto my_timeout = detail::to_timeout_point(timeout);
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
    if (auto r = promises.reserve(std::ranges::distance(first, last) + !!my_timeout, my_promise.alloc);
          !r)
      co_return {unexpect, r.error()};

    for (auto i = first; i != last; ++i)
      (void)promises.push_back(
          static_cast<olifilo::detail::promise_wait_callgraph*>(
            *i && !i->done() ? &i->handle.promise() : nullptr
          ), my_promise.alloc);

    if (auto r = co_await wait_t::operator()(promises, wait_until, my_timeout);
        !r)
      co_return {unexpect, r.error()};
    else
      co_return {std::in_place, std::next(std::move(first), *r)};
  }

  /**
   * @param wait_until  condition to wait on (all/any)
   * @param futures     set of futures to wait on
   * @param timeout     limit waiting to not exceed this time
   */
  template <std::ranges::forward_range R, detail::timeout Timeout = std::optional<clock::time_point>>
  requires(is_future_v<std::ranges::range_value_t<R>>)
  future<std::ranges::iterator_t<R>>
    static constexpr operator()(
      until                            wait_until
    , R&                               futures
    , Timeout                          timeout = {}
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
