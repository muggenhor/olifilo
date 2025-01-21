// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <chrono>
#include <limits>
#include <optional>
#include <system_error>

#include <sys/select.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
inline expected<unsigned> select(
    unsigned nfds
  , ::fd_set* readfds
  , ::fd_set* writefds
  , ::fd_set* exceptfds
  , struct ::timeval* timeout = nullptr
  ) noexcept
{
  if (nfds > static_cast<unsigned>(std::numeric_limits<int>::max()))
    return std::make_error_code(std::errc::invalid_argument);

  if (nfds > FD_SETSIZE)
    return std::make_error_code(std::errc::bad_file_descriptor);

  if (auto rv = ::select(static_cast<int>(nfds), readfds, writefds, exceptfds, timeout); rv < 0)
    return std::error_code(errno, std::system_category());
  else
    return rv;
}

inline expected<unsigned> select(
    unsigned nfds
  , ::fd_set* readfds
  , ::fd_set* writefds
  , ::fd_set* exceptfds
  , std::chrono::microseconds timeout
  ) noexcept
{
  struct ::timeval tv{
    .tv_sec = timeout.count() / 1000000L,
    .tv_usec = static_cast<suseconds_t>(timeout.count() % 1000000L),
  };
  return select(nfds, readfds, writefds, exceptfds, &tv);
}

template <typename Clock, typename Duration = typename Clock::duration>
expected<unsigned> select(
    unsigned nfds
  , ::fd_set* readfds
  , ::fd_set* writefds
  , ::fd_set* exceptfds
  , std::chrono::time_point<Clock, Duration> timeout
  ) noexcept
{
  const auto now = Clock::now();
  const auto time_left = std::chrono::duration_cast<std::chrono::microseconds>(timeout - now);
  return select(nfds, readfds, writefds, exceptfds, time_left);
}

inline expected<unsigned> select(unsigned nfds
  , ::fd_set* readfds
  , ::fd_set* writefds
  , ::fd_set* exceptfds
  , std::optional<std::chrono::microseconds> timeout
  ) noexcept
{
  if (timeout)
    return select(nfds, readfds, writefds, exceptfds, *timeout);
  else
    return select(nfds, readfds, writefds, exceptfds);
}

template <typename Clock, typename Duration = typename Clock::duration>
expected<unsigned> select(
    unsigned nfds
  , ::fd_set* readfds
  , ::fd_set* writefds
  , ::fd_set* exceptfds
  , std::optional<std::chrono::time_point<Clock, Duration>> timeout
  ) noexcept
{
  if (timeout)
    return select(nfds, readfds, writefds, exceptfds, *timeout);
  else
    return select(nfds, readfds, writefds, exceptfds);
}
}  // namespace olifilo::io
