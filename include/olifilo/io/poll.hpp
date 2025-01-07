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

  using enum poll_event;

  explicit constexpr poll(file_descriptor_handle fd, poll_event events) noexcept
    : fd(fd)
    , events(events)
  {
  }

  explicit constexpr poll(file_descriptor_handle fd, poll_event events, timeout_clock::time_point timeout) noexcept
    : fd(fd)
    , events(events)
    , timeout(timeout)
  {
  }

  explicit constexpr poll(file_descriptor_handle fd, poll_event events, timeout_clock::duration timeout) noexcept
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

  file_descriptor_handle fd;
  poll_event events = static_cast<poll_event>(0);
  std::optional<timeout_clock::time_point> timeout;
};
}  // namespace olifilo::io
