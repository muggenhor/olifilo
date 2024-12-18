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
class [[nodiscard("future not awaited")]] future
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

    constexpr expected<T> await_resume() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
      if (!handle)
        return {unexpect, make_error_code(error::future_already_retrieved)};

      assert(handle.done());
      auto& promise = handle.promise();

      expected<T> rv(std::move(promise.returned_value));
      destroy();
      return rv;
    }

    expected<T> get() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
      if (!handle)
        return {unexpect, make_error_code(error::future_already_retrieved)};

      auto& promise = handle.promise();
      detail::io_poll_context executor;

      while (!handle.done())
      {
        if (auto err = executor(promise); err)
          return {unexpect, err};
      }

      return await_resume();
    }

  private:
    template <typename U>
    friend class detail::promise;
    friend wait_t;

    constexpr future(std::coroutine_handle<detail::promise<T>> handle) noexcept
      : handle(handle)
    {
    }

    std::coroutine_handle<detail::promise<T>> handle;
};
}  // namespace olifilo
