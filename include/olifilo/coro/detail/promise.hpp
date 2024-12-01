// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <coroutine>
#include <deque>
#include <optional>
#include <system_error>
#include <utility>

#include "forward.hpp"

#include <olifilo/expected.hpp>
#include <olifilo/io/poll.hpp>

namespace olifilo
{
class my_current_promise
{
  private:
    my_current_promise() = default;

    // Only friends are allowed to know the promise they run in
    friend when_all_t;
};
}

namespace olifilo::detail
{
struct awaitable_poll;

struct promise_wait_callgraph
{
  promise_wait_callgraph* root_caller;
  std::deque<promise_wait_callgraph*> callees;
  std::coroutine_handle<> waits_on_me;
  std::deque<awaitable_poll*> events;

  constexpr promise_wait_callgraph() noexcept
    : root_caller(this)
  {
  }

  ~promise_wait_callgraph()
  {
    std::erase(root_caller->callees, this);
  }

  promise_wait_callgraph(promise_wait_callgraph&&) = delete;
  promise_wait_callgraph& operator=(promise_wait_callgraph&&) = delete;
};

struct awaitable_poll : private io::poll
{
  using poll::fd;
  using poll::events;
  using poll::timeout;

  expected<void> wait_result;
  std::coroutine_handle<detail::promise_wait_callgraph> waiter;

  // We need the location/address of this struct to be stable, so prohibit copying.
  // But we're still allowing the copy constructor to be callable (but *not* actually called!) by our factory function
  constexpr awaitable_poll(awaitable_poll&& rhs) = delete;
  awaitable_poll& operator=(const awaitable_poll&) = delete;

  explicit constexpr awaitable_poll(const poll& event, std::coroutine_handle<detail::promise_wait_callgraph> waiter) noexcept
    : poll(event)
    , waiter(waiter)
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

  void await_suspend(std::coroutine_handle<> suspended) noexcept
  {
    assert(waiter == suspended && "await_transform called from different coroutine than await_suspend!");

    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(event@{}=({}, fd={}, waiter={}))\n", ts(), __LINE__, "awaitable_poll::await_suspend", static_cast<const void*>(this), this->events, this->fd, this->waiter.address());

    // NOTE: have to do this here, instead of await_transform, because we can only know the address of 'this' here
    waiter.promise().root_caller->events.push_back(this);
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
class promise : private detail::promise_wait_callgraph
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
          returned_value = exc.error();
        }
        catch (std::system_error& exc)
        {
          returned_value = exc.code();
        }
      }
      else
      {
        std::terminate();
      }
    }

    constexpr void return_value(expected<T>&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
      // NOTE: MUST use assignment (instead of .emplace()) to be safe in case of self-assignment
      returned_value = std::move(v);
    }

    template <typename U>
    constexpr future<U>&& await_transform(future<U>&& fut) noexcept
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("await_transform"));

      if (detail::promise_wait_callgraph* const callee_promise = fut.handle ? &fut.handle.promise() : nullptr)
      {
        if (std::ranges::find(callees, callee_promise) == callees.end())
          callees.push_back(callee_promise);

        auto& events_queue = root_caller->events;

        for (std::size_t i = 0; i < callees.size(); ++i)
        {
          // Recurse into grand children
          {
            auto grand_children = std::move(callees[i]->callees);
            callees.insert(callees.end(), begin(grand_children), end(grand_children));
          }

          const auto& child = callees[i];

          child->root_caller = this;
          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](child={}, child->root={}\n", ts(), __LINE__, func_name, i, std::coroutine_handle<promise>::from_promise(*reinterpret_cast<promise*>(child)).address(), std::coroutine_handle<promise>::from_promise(*reinterpret_cast<promise*>(child->root_caller)).address());

          // Steal all events from all child futures
          if (events_queue.empty())
          {
            events_queue = std::move(child->events);
          }
          else
          {
            // move entire container to ensure memory is recovered at scope exit
            auto moved_events = std::move(child->events);
            events_queue.insert(events_queue.end(), begin(moved_events), end(moved_events));
          }
        }
      }
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(root={}, me={})\n", ts(), __LINE__, func_name, std::coroutine_handle<promise>::from_promise(*static_cast<promise*>(root_caller)).address(), std::coroutine_handle<promise>::from_promise(*this).address());
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
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("await_transform"));
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(root={}, me={})\n", ts(), __LINE__, func_name, std::coroutine_handle<promise>::from_promise(*static_cast<promise*>(root_caller)).address(), std::coroutine_handle<promise>::from_promise(*this).address());
      return awaitable_poll(std::move(obj), std::coroutine_handle<detail::promise_wait_callgraph>::from_promise(*this));
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

    constexpr auto await_transform(my_current_promise&&) noexcept
    {
      return promise_retriever{*this};
    }

    template <typename U>
    friend class promise;
    friend class future<T>;
    friend when_all_t;

  private:
    std::optional<expected<T>> returned_value;
};
}  // namespace olifilo::detail
