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
      destroy();
      handle = std::exchange(rhs.handle, nullptr);

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
      return handle.address() == detail::noop_coro_handle.address() || (handle && handle.done());
    }

    constexpr bool await_ready() const noexcept
    {
      return handle.address() == detail::noop_coro_handle.address() || !handle || handle.done();
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
      else if (handle.address() == detail::noop_coro_handle.address())
        return {unexpect, make_error_code(error::coro_bad_alloc)};

      assert(handle.done());
      auto& promise = handle.promise();

      // Trick to keep guaranteed copy elision by not storing 'returned_value' in a local var first
      struct scope_exit
      {
        future& self;
        ~scope_exit()
        {
          self.destroy();
        }
      } scope_exit(*this);

      return std::move(promise.returned_value);
    }

    expected<T> get() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
      if (!handle)
        return {unexpect, make_error_code(error::future_already_retrieved)};
      else if (handle.address() == detail::noop_coro_handle.address())
        return {unexpect, make_error_code(error::coro_bad_alloc)};

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

    constexpr future(std::coroutine_handle<promise_type> handle) noexcept
      : handle(handle)
    {
    }

    constexpr void destroy()
    {
      if (handle)
      {
        handle.destroy();
        handle = nullptr;
      }
    }

    std::coroutine_handle<promise_type> handle;
};
}  // namespace olifilo
