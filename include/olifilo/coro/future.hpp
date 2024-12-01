// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <coroutine>

#include "../expected.hpp"

#include "detail/forward.hpp"
#include "detail/io_poll_context.hpp"
#include "detail/promise.hpp"

namespace olifilo
{
template <typename T>
class future
{
  public:
    using value_type = expected<T>;
    using promise_type = detail::promise<T>;

    constexpr future(future&& rhs) noexcept
      : handle(std::exchange(rhs.handle, nullptr))
    {
    }

    future& operator=(future&& rhs) noexcept
    {
      if (&rhs != this)
      {
        destroy();
        handle = std::exchange(rhs.handle, nullptr);
      }

      return *this;
    }

    ~future()
    {
      destroy();
    }

    constexpr explicit operator bool() const noexcept
    {
      return static_cast<bool>(handle);
    }

    bool done() const
    {
      return handle && handle.done();
    }

    void destroy()
    {
      if (handle)
      {
        handle.destroy();
        handle = nullptr;
      }
    }

    expected<T> get() noexcept
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("get"));

      assert(handle);

      auto& promise = handle.promise();
      detail::io_poll_context executor;

      ////unsigned i = 0;

      while (!handle.done())
      {
        ////unsigned j = 0;

        assert(!promise.events.empty() || !executor.empty());

        ////const auto now = std::decay_t<decltype(*promise.events.front()->timeout)>::clock::now();
        for (const auto event : promise.events)
        {
          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{},{}](root={}, events@{}, event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, i++, j++, handle.address(), static_cast<const void*>(&promise.events), static_cast<const void*>(event), event->events, event->fd, event->timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), event->waiter.address());
          executor.wait_for(*event);
        }
        promise.events.clear();

        if (auto err = executor.run_one(); err)
          return unexpected(err);
      }

      assert(promise.returned_value);
      expected<T> rv(std::move(*promise.returned_value));
      destroy();
      return rv;
    }

    constexpr bool await_ready() const noexcept
    {
      return !handle || handle.done();
    }

    void await_suspend(std::coroutine_handle<> suspended)
    {
      auto& promise = handle.promise();
      assert(promise.waits_on_me == nullptr && "only a single coroutine should have a future for this coroutine to co_await on!");
      promise.waits_on_me = suspended;
    }

    constexpr expected<T> await_resume() noexcept
    {
      assert(handle);
      assert(handle.done());
      auto& promise = handle.promise();

      assert(promise.returned_value);
      expected<T> rv(std::move(*promise.returned_value));
      destroy();
      return rv;
    }

  private:
    template <typename U>
    friend class detail::promise;

    constexpr future(std::coroutine_handle<detail::promise<T>> handle) noexcept
      : handle(handle)
    {
    }

    std::coroutine_handle<detail::promise<T>> handle;
};
}  // namespace olifilo
