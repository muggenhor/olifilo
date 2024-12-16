// SPDX-License-Identifier: GPL-3.0-or-later

#include "olifilo/coro/wait.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>

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

  auto& my_promise = co_await detail::current_promise();

  // Placeholder that's safe for our executors while still occupying a slot to ensure a one-to-one mapping of positions in 'callees' and 'promises'.
  static constinit const detail::promise_wait_callgraph nop_always_ready_promise;

  /*****************************************************************************************
   * Prepare our own promise to be ready for the executor to find all events and resume us *
   *****************************************************************************************/

  // Borrow input promises list as our own promise's callee list to avoid allocating
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

        // Restore promises to being the top of their respective await call graphs with nothing waiting on them (anymore)
        future->waits_on_me = nullptr;
      }
    }
  } scope_exit(my_promise.callees, promises);

  constexpr auto ready = [] (auto future) noexcept { return future->waits_on_me == nullptr; };

  assert(ready(&nop_always_ready_promise));

  // Simulate promise.await_transform(promises)... We can't use co_await because it would wait on *all* promises (in order, one by one).
  const auto me = std::coroutine_handle<std::remove_cvref_t<decltype(my_promise)>>::from_promise(my_promise);
  for (auto*& future : my_promise.callees)
  {
    if (const bool ready_input = future == nullptr;
        ready_input)
    {
      // const_cast is safe because this function takes care not to modify it through this list
      // And executors are only allowed to *remove* entries from the 'events' list, which for this NOP promise is empty.
      future = const_cast<detail::promise_wait_callgraph*>(&nop_always_ready_promise);
      continue;
    }

    assert(future->waits_on_me == nullptr && "internal logic error: not allowed to await promises already being awaited");
    assert(future->caller == nullptr && "stealing a future someone else is waiting on");
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

  /***************************************************************************************************
   * Scan the list of promises to see if enough are ready, suspend ourselves, detect timeout, repeat *
   ***************************************************************************************************/

  while (true)
  {
    assert(std::ranges::empty(my_promise.events) && "TODO: test timeouts!");

    bool all_ready = true;
    for (auto future = my_promise.callees.begin(); future != my_promise.callees.end(); ++future)
    {
      assert(ready(*future) || (*future)->waits_on_me == me);

      if (!ready(*future))
        all_ready = false;
      else if (const auto index = static_cast<std::size_t>(future - my_promise.callees.begin());
          wait_until == until::first_completed)
        co_return {std::in_place, index};
    }
    if (all_ready)
      co_return {std::in_place, 0};

    // Now allow this future's .get() to handle the actual I/O multiplexing
    co_await std::suspend_always();

    if (timeout_event
     && timeout_event->waiter == nullptr)
    {
      // Timeout occurred
      assert(std::ranges::find(my_promise.events, &*timeout_event) == my_promise.events.end() && "executor should have removed timeout event from our wait list");
      co_return {unexpect, timeout_event->wait_result.error()};
    }
  }
}
}  // namespace olifilo
