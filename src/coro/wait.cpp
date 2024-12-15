// SPDX-License-Identifier: GPL-3.0-or-later

#include "olifilo/coro/wait.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>

#include <olifilo/io/poll.hpp>

namespace olifilo
{
future<std::size_t>
  wait_t::operator()(
    detail::sbo_vector<detail::promise_wait_callgraph*>&       promises
  , until                                                const wait_until
  , std::optional<std::chrono::steady_clock::time_point> const timeout
  ) noexcept
{
  // Special case because the code below assumes that .begin() + 1 and .end() - 1 are valid iterators.
  if (promises.empty())
    co_return {std::in_place, 0};

  constexpr auto ready_input = [] (auto fut) noexcept { return fut == nullptr; };
  constexpr auto not_ready_input = [ready_input] (auto fut) noexcept { return !ready_input(fut); };

  expected<std::size_t> first_ready_future = std::ranges::find_if(promises, ready_input) - promises.begin();
  // Special case to avoid even looking at other promises if we need a single and found one ready
  if (wait_until == until::first_completed
   && *first_ready_future != promises.size())
    co_return first_ready_future;

  const auto first_not_ready_future_index = *first_ready_future != 0
    ? 0
    : std::ranges::find_if(promises.begin() + 1, promises.end(), not_ready_input) - promises.begin()
    ;
  assert(first_not_ready_future_index == std::ranges::find_if(promises, not_ready_input) - promises.begin());
  const auto last_not_ready_future_index = std::ranges::find_if(
      std::reverse_iterator(promises.end())
    , std::reverse_iterator(promises.begin() + first_not_ready_future_index)
    , not_ready_input
    ).base() - promises.begin();
  assert(last_not_ready_future_index == std::ranges::find_if(promises | std::ranges::views::reverse, not_ready_input).base() - promises.begin());
  assert(first_not_ready_future_index <= last_not_ready_future_index);

  auto& my_promise = co_await detail::current_promise();
  const auto me = std::coroutine_handle<std::remove_cvref_t<decltype(my_promise)>>::from_promise(my_promise);

  // Placeholder that's safe for our executors while still occupying a slot to ensure a one-to-one mapping of positions in 'callees' and 'promises'.
  static constinit const detail::promise_wait_callgraph nop_always_ready_promise;

  // 
  unsafe_swap(my_promise.callees, promises);
  struct scope_exit
  {
    decltype(my_promise.callees)& callees_;
    decltype(promises)& futures_;
    ~scope_exit()
    {
      unsafe_swap(callees_, futures_);

      for (auto*& future : futures_)
      {
        // Hide our NOP future from callers and restore the nullptr they gave us
        if (future == &nop_always_ready_promise)
          future = nullptr;
        if (!future)
          continue;

        // Restore any remaining promises to the top of their callgraphs
        future->caller = nullptr;
        future->waits_on_me = nullptr;
      }
    }
  } scope_exit(my_promise.callees, promises);

  constexpr auto ready = [] (auto future) noexcept { return future->caller == nullptr; };
  constexpr auto not_ready = [ready] (auto future) noexcept { return !ready(future); };

  assert(ready(&nop_always_ready_promise));

  // Simulate promise.await_transform(promises)... We can't use co_await because it would wait on *all* promises (in order, one by one).
  for (auto*& future : my_promise.callees)
  {
    if (ready_input(future))
    {
      // const_cast is safe because this function won't modify anything with caller == nullptr (the default).
      // And executors are only allowed to *remove* entries from the 'events' list, which for this is empty.
      future = const_cast<detail::promise_wait_callgraph*>(&nop_always_ready_promise);
      continue;
    }

    assert(future->waits_on_me == nullptr && "internal logic error: not allowed to await promises already being awaited");
    assert(future->caller == nullptr && "stealing a future someone else is waiting on");
    future->caller = &my_promise;
    future->waits_on_me = me;
  }

  std::optional<detail::awaitable_poll> timeout_event;
  if (timeout)
  {
    timeout_event.emplace(io::poll(*timeout));
    timeout_event->waiter = me;

    if (auto r = my_promise.events.push_back(&*timeout_event, my_promise.alloc);
        !r)
      co_return {unexpect, r.error()};
  }

  // Restore promises to being the top of their respective await call graphs with nothing waiting on them (anymore)
  const auto mark_ready = [=, caller = &my_promise] (auto future) noexcept {
    assert(future->caller == caller);
    assert(future->waits_on_me == nullptr || future->waits_on_me == me);
    future->caller = nullptr;
    future->waits_on_me = nullptr;
  };

  auto first_not_ready_future = my_promise.callees.begin() + first_not_ready_future_index;
  auto last_not_ready_future = my_promise.callees.begin() + last_not_ready_future_index;
  while (first_not_ready_future != last_not_ready_future)
  {
    assert(my_promise.callees.begin() <= first_not_ready_future);
    assert(first_not_ready_future < last_not_ready_future);
    assert(last_not_ready_future <= my_promise.callees.end());

    assert(std::ranges::empty(my_promise.events) && "TODO: test timeouts!");
    assert(first_ready_future);
    assert(wait_until != until::first_completed || *first_ready_future == my_promise.callees.size());
    // Now allow this future's .get() to handle the actual I/O multiplexing
    co_await std::suspend_always();

    if (timeout_event
     && timeout_event->waiter == nullptr)
    {
      // Timeout occurred
      first_ready_future = {unexpect, timeout_event->wait_result.error()};
      assert(std::ranges::find(my_promise.events, &*timeout_event) == my_promise.events.end() && "executor should have removed timeout event from our wait list");
      goto stop_waiting;
    }

    for (auto future = first_not_ready_future; future != last_not_ready_future; ++future)
    {
      if (not_ready(*future))
      {
        assert((*future)->caller == &my_promise);
        assert((*future)->waits_on_me == nullptr || (*future)->waits_on_me == me);

        if (const bool became_ready = (*future)->waits_on_me == nullptr;
            became_ready)
        {
          if (const auto index = static_cast<std::size_t>(future - my_promise.callees.begin());
              *first_ready_future > index)
          {
            first_ready_future.emplace(index);
            if (wait_until == until::first_completed)
              goto stop_waiting;
          }

          mark_ready(*future);
        }
      }

      // Update list [begin,end] markers to ensure they'll be equal when we only have ready promises left
      if (ready(*future))
      {
        if (future == first_not_ready_future)
        {
          ++first_not_ready_future;
        }
        else if (future + 1 == last_not_ready_future)
        {
          --last_not_ready_future;
          break;
        }
      }
    }
  }
stop_waiting:
  assert(!first_ready_future || *first_ready_future != my_promise.callees.size());
  assert(!first_ready_future || wait_until != until::all_completed || *first_ready_future == 0);

  co_return first_ready_future;
}
}  // namespace olifilo
