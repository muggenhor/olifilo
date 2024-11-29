// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <optional>

#include "types.hpp"

namespace olifilo::io
{
class poll
{
  public:
    using timeout_clock = std::chrono::steady_clock;

    explicit constexpr poll(file_descriptor_handle fd, io::poll_event events) noexcept
      : _fd(fd)
      , _events(events)
    {
    }

    explicit constexpr poll(file_descriptor_handle fd, io::poll_event events, timeout_clock::time_point timeout) noexcept
      : _fd(fd)
      , _events(events)
      , _timeout(timeout)
    {
    }

    explicit constexpr poll(file_descriptor_handle fd, io::poll_event events, timeout_clock::duration timeout) noexcept
      : poll(fd, events, timeout_clock::now() + timeout)
    {
    }

    explicit constexpr poll(timeout_clock::time_point timeout) noexcept
      : _timeout(timeout)
    {
    }

    explicit constexpr poll(timeout_clock::duration timeout) noexcept
      : poll(timeout_clock::now() + timeout)
    {
    }

    // FIXME: move this type into promise and make its await_transform this type's factory function
    struct awaitable;
    constexpr awaitable operator co_await() noexcept;

  private:
    file_descriptor_handle _fd = invalid_file_descriptor_handle;
    io::poll_event _events = static_cast<io::poll_event>(0);
    std::optional<timeout_clock::time_point> _timeout;
};
}  // namespace olifilo::io

#include <coroutine>
#include <deque>

// FIXME: move this type into promise and make its await_transform this type's factory function
namespace olifilo::io
{
struct poll::awaitable
{
  file_descriptor_handle fd = invalid_file_descriptor_handle;
  io::poll_event event;
  std::optional<timeout_clock::time_point> timeout;
  expected<void> wait_result;
  std::coroutine_handle<> waiter;
  std::deque<awaitable*>* waits_on = nullptr;

  awaitable() = default;

  explicit constexpr awaitable(const poll& event) noexcept
    : fd(event._fd)
    , event(event._events)
    , timeout(event._timeout)
  {
  }

  explicit constexpr awaitable(const poll& event, std::deque<awaitable*>& waits_on) noexcept
    : fd(event._fd)
    , event(event._events)
    , timeout(event._timeout)
    , waits_on(&waits_on)
  {
  }

  constexpr bool await_ready() const noexcept
  {
    return false;
  }

  constexpr auto await_resume() const noexcept
  {
    return std::move(wait_result);
  }

  void await_suspend(std::coroutine_handle<> suspended)
  {
    waiter = suspended;

    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(events@{}, event@{}=({}, fd={}, waiter={}))\n", ts(), __LINE__, "poll::awaitable::await_suspend", static_cast<const void*>(waits_on), static_cast<const void*>(this), this->event, this->fd, this->waiter.address());

    if (waits_on)
      waits_on->push_back(this);
  }
};

inline constexpr poll::awaitable poll::operator co_await() noexcept
{
  return awaitable(*this);
}
}  // namespace olifilo::io
