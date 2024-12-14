// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <coroutine>
#include <exception>
#include <system_error>
#include <type_traits>
#include <utility>

#include "forward.hpp"

#include <olifilo/detail/small_vector.hpp>
#include <olifilo/expected.hpp>
#include <olifilo/errors.hpp>
#include <olifilo/io/poll.hpp>

namespace olifilo::detail
{
class current_promise
{
  private:
    current_promise() = default;

    // Only friends are allowed to know the promise they run in
    friend when_all_t;
    friend when_any_t;
};

struct awaitable_poll;

struct promise_wait_callgraph
{
  using allocator_type = std::allocator<void*>;

  promise_wait_callgraph* caller;
  sbo_vector<promise_wait_callgraph*> callees;
  std::coroutine_handle<> waits_on_me;
  sbo_vector<awaitable_poll*> events;
  [[no_unique_address]] allocator_type alloc;

  constexpr promise_wait_callgraph() noexcept
    : caller(this)
  {
  }

  ~promise_wait_callgraph()
  {
    erase(caller->callees, this);
    callees.destroy(alloc);
    events.destroy(alloc);
  }

  promise_wait_callgraph(promise_wait_callgraph&&) = delete;
  promise_wait_callgraph& operator=(promise_wait_callgraph&&) = delete;
};

struct awaitable_poll : private io::poll
{
  using poll::fd;
  using poll::events;
  using poll::timeout;

  expected<void> wait_result = {unexpect, error::uninitialized};
  std::coroutine_handle<> waiter;

  // We need the location/address of this struct to be stable, so prohibit copying.
  // But we're still allowing the copy constructor to be callable (but *not* actually called!) by our factory function
  constexpr awaitable_poll(awaitable_poll&& rhs) = delete;
  awaitable_poll& operator=(const awaitable_poll&) = delete;

  explicit constexpr awaitable_poll(const poll& event) noexcept
    : poll(event)
  {
  }

  constexpr bool await_ready() const noexcept
  {
    return false;
  }

  constexpr auto await_resume() const
  {
    // Simple cancellation implementation that'll propagate through the coroutine stack.
    // It's caught by unhandled_exception and stored again as error_code there
    if (!wait_result && wait_result.error() == std::errc::operation_canceled)
      throw std::system_error(wait_result.error(), "io::poll");

    return std::move(wait_result);
  }

  template <typename Promise>
  requires(std::is_base_of_v<promise_wait_callgraph, Promise>)
  std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> suspended) noexcept
  {
    assert(waiter == nullptr && "may only await once");
    waiter = suspended;

    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(event@{}=({}, fd={}, waiter={}))\n", ts(), __LINE__, "awaitable_poll::await_suspend", static_cast<const void*>(this), this->events, this->fd, this->waiter.address());

    // NOTE: have to do this here, instead of await_transform, because we can only know the address of 'this' here
    auto& promise = suspended.promise();
    if (auto r = promise.events.push_back(this, promise.alloc);
        !r)
    {
      wait_result = {unexpect, r.error()};
      return std::exchange(waiter, nullptr);
    }

    return std::noop_coroutine();
  }
};

struct suspend_always_to
{
  std::coroutine_handle<> waiter;

  constexpr bool await_ready() const noexcept { return false; }
  constexpr void await_resume() const noexcept { }

  constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<> suspended [[maybe_unused]]) noexcept
  {
    if (!waiter)
      return std::noop_coroutine();
    return std::exchange(waiter, nullptr);
  }
};

template <typename T>
class promise final : private detail::promise_wait_callgraph
{
  public:
    future<T> get_return_object() { return future<T>(std::coroutine_handle<promise>::from_promise(*this)); }
    constexpr std::suspend_never initial_suspend() noexcept { return {}; }
    constexpr suspend_always_to final_suspend() noexcept { return {std::exchange(waits_on_me, nullptr)}; }

    constexpr void unhandled_exception() noexcept
    {
      if constexpr (is_expected_with_std_error_code_v<T>)
      {
        try
        {
          throw;
        }
        catch (std::bad_expected_access<std::error_code>& exc)
        {
          returned_value = {unexpect, exc.error()};
        }
        catch (std::system_error& exc)
        {
          returned_value = {unexpect, exc.code()};
        }
      }
      else
      {
        std::terminate();
      }
    }

    constexpr void return_value(expected<T>&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
      // NOTE: be safe against self-assignment in case of internal implementations that steal returned_value's storage
      if (&returned_value != &v)
      {
        returned_value = std::move(v);
      }
    }

    template <typename U>
    constexpr future<U>&& await_transform(future<U>&& fut) noexcept
    {
      if (detail::promise_wait_callgraph* const callee_promise = fut.handle ? &fut.handle.promise() : nullptr)
      {
        assert(std::ranges::find(callees, callee_promise) == callees.end());
        if (!callees.push_back(callee_promise, alloc))
        {
          assert(!"awaiting a single future shouldn't require memory to be allocated, so allocation failures shouldn't happen!");
          std::terminate();
        }
        assert(callee_promise->caller == callee_promise && "stealing a future someone else is waiting on");
        callee_promise->caller = this;
      }
      return std::move(fut);
    }

    template <typename MaybeAwaitable>
    requires(!std::is_same_v<std::decay_t<MaybeAwaitable>, io::poll>)
    constexpr MaybeAwaitable&& await_transform(MaybeAwaitable&& obj) noexcept
    {
      return std::forward<MaybeAwaitable>(obj);
    }

    constexpr auto await_transform(io::poll&& obj) noexcept
    {
      return awaitable_poll(std::move(obj));
    }

    struct promise_retriever
    {
      promise& p;

      constexpr bool await_ready() const noexcept
      {
        return true;
      }

      constexpr auto await_suspend(std::coroutine_handle<> suspended) noexcept
      {
        return suspended;
      }

      constexpr promise& await_resume() const noexcept
      {
        return p;
      }
    };

    constexpr auto await_transform(current_promise&&) noexcept
    {
      return promise_retriever{*this};
    }

    template <typename U>
    friend class promise;
    friend class future<T>;
    friend awaitable_poll;
    friend when_all_t;
    friend when_any_t;

  private:
    expected<T> returned_value = {unexpect, error::broken_promise};
};
}  // namespace olifilo::detail
