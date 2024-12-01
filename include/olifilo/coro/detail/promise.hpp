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
struct promise_wait_callgraph
{
  promise_wait_callgraph* root_caller;
  std::deque<promise_wait_callgraph*> callees;
  std::coroutine_handle<> waits_on_me;
  std::deque<io::poll::awaitable*> events;

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

constexpr void push_back(std::coroutine_handle<promise_wait_callgraph> waiter, io::poll::awaitable& event) noexcept
{
  waiter.promise().root_caller->events.push_back(&event);
}

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
      return io::poll::awaitable(std::move(obj), std::coroutine_handle<detail::promise_wait_callgraph>::from_promise(*this));
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
