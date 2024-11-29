// SPDX-License-Identifier: GPL-3.0-or-later

// Better answer to https://stackoverflow.com/questions/66018429/most-efficient-way-to-get-coroutine-handle-from-within-a-c-coroutine

#include <cassert>
#include <coroutine>

template <typename Promise = void>
struct co_gethandle_t
{
    std::coroutine_handle<Promise> handle;

    constexpr bool await_ready() const noexcept
    {
      if constexpr (std::is_same_v<Promise, void>)
        return false;
      else
        return static_cast<bool>(handle);
    }

    constexpr bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
      if constexpr (std::is_same_v<Promise, void>)
        this->handle = handle;
      else
        assert(this->handle);

      return false;
    }

    constexpr auto await_resume() noexcept
    {
      return handle;
    }
};

constexpr auto co_gethandle() noexcept
{
  return co_gethandle_t<>();
}

template <typename DerivedPromise>
struct co_gethandle_promise_support
{
  constexpr co_gethandle_t<DerivedPromise> await_transform(co_gethandle_t<>) noexcept
  {
    return {
      .handle = std::coroutine_handle<DerivedPromise>::from_promise(static_cast<DerivedPromise&>(*this)),
    };
  }

  // fallback passthrough overload when co_await-ing other types
  template <typename T>
    requires(!std::is_same_v<std::decay_t<T>, co_gethandle_t<>>)
  constexpr T&& await_transform(T&& obj) const noexcept
  {
    return static_cast<T&&>(obj);
  }
};

#if TEST
#include <exception>
#include <cstdint>

namespace test
{

struct promise;

struct future
{
  using promise_type = promise;

  ~future()
  {
    if (handle)
      handle.destroy();
  }

  std::uintptr_t get();

  std::coroutine_handle<promise_type> handle;
};

struct promise : private co_gethandle_promise_support<promise>
{
  friend struct co_gethandle_promise_support<promise>; // MUST be friend to allow it to down cast '*this'
  using co_gethandle_promise_support<promise>::await_transform;

  constexpr std::suspend_never initial_suspend() noexcept { return {}; }
  constexpr std::suspend_always final_suspend() noexcept { return {}; }
  [[noreturn]] void unhandled_exception() const noexcept { std::terminate(); }
  future get_return_object() noexcept
  {
    return {std::coroutine_handle<promise>::from_promise(*this),};
  }

  constexpr void return_value(std::uintptr_t value) noexcept { this->value = value; }
  std::uintptr_t value;
};

inline std::uintptr_t future::get()
{
  assert(handle);
  assert(handle.done());
  return handle.promise().value;
}

future get_handle_test()
{
  auto handle = co_await co_gethandle();
  auto& promise = handle.promise();
  co_return reinterpret_cast<std::uintptr_t>(handle.address());
}

}  // namespace test

#include <iostream>

int main()
{
  const auto dangling_handle = test::get_handle_test().get();
  std::cerr << "handle value was: 0x" << std::hex << dangling_handle << '\n';
}
#endif
