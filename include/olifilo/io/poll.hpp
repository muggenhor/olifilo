// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <optional>

#include "types.hpp"

namespace olifilo::io
{
struct poll
{
  using timeout_clock = std::chrono::steady_clock;

  explicit constexpr poll(file_descriptor_handle fd, io::poll_event events) noexcept
    : fd(fd)
    , events(events)
  {
  }

  explicit constexpr poll(file_descriptor_handle fd, io::poll_event events, timeout_clock::time_point timeout) noexcept
    : fd(fd)
    , events(events)
    , timeout(timeout)
  {
  }

  explicit constexpr poll(file_descriptor_handle fd, io::poll_event events, timeout_clock::duration timeout) noexcept
    : poll(fd, events, timeout_clock::now() + timeout)
  {
  }

  explicit constexpr poll(timeout_clock::time_point timeout) noexcept
    : timeout(timeout)
  {
  }

  explicit constexpr poll(timeout_clock::duration timeout) noexcept
    : poll(timeout_clock::now() + timeout)
  {
  }

  // FIXME: move this type into promise and make its await_transform this type's factory function
  struct awaitable;

  file_descriptor_handle fd;
  io::poll_event events = static_cast<io::poll_event>(0);
  std::optional<timeout_clock::time_point> timeout;
};
}  // namespace olifilo::io

#include <coroutine>
#include <deque>

// FIXME: move this type into promise and make its await_transform this type's factory function
namespace olifilo::detail
{
class promise_wait_callgraph;
constexpr void push_back(std::coroutine_handle<promise_wait_callgraph> waiter, io::poll::awaitable& event) noexcept;
}

// FIXME: move this type into promise and make its await_transform this type's factory function
namespace olifilo::io
{
struct poll::awaitable : private poll
{
  using poll::fd;
  using poll::events;
  using poll::timeout;

  expected<void> wait_result;
  std::coroutine_handle<detail::promise_wait_callgraph> waiter;

  // We need the location/address of this struct to be stable, so prohibit copying.
  // But we're still allowing the copy constructor to be callable (but *not* actually called!) by our factory function
  constexpr awaitable(awaitable&& rhs) = delete;
  awaitable& operator=(const awaitable&) = delete;

  explicit constexpr awaitable(const poll& event, std::coroutine_handle<detail::promise_wait_callgraph> waiter) noexcept
    : poll(event)
    , waiter(waiter)
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

  void await_suspend(std::coroutine_handle<> suspended) noexcept
  {
    assert(waiter == suspended && "await_transform called from different coroutine than await_suspend!");

    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(event@{}=({}, fd={}, waiter={}))\n", ts(), __LINE__, "poll::awaitable::await_suspend", static_cast<const void*>(this), this->events, this->fd, this->waiter.address());

    // NOTE: have to do this here, instead of await_transform, because we can only know the address of 'this' here
    detail::push_back(waiter, *this);
  }
};
}  // namespace olifilo::io
