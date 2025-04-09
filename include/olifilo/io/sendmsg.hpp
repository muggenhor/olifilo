// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <cstddef>
#include <limits>
#include <span>
#include <system_error>

#include <sys/socket.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
inline expected<std::size_t> sendmsg(file_descriptor_handle fd, std::span<const std::span<const std::byte>> bufs, int flags) noexcept
{
  // While this is size_t on POSIX, it's a custom type on lwIP
  using iovlen_t = decltype(::msghdr::msg_iovlen);

  if (bufs.size() > static_cast<std::size_t>(std::numeric_limits<iovlen_t>::max()))
    return {olifilo::unexpect, make_error_code(std::errc::message_size)};

  // FIXME: this relies on 'struct iovec' and 'std::span<T>' having the same layout. While currently true on
  //        libstdc++ (GCC 14) we might wish to static_assert that somehow.
  const ::msghdr msg = {
    .msg_iov = const_cast<::iovec*>(reinterpret_cast<const ::iovec*>(bufs.data())),
    .msg_iovlen = static_cast<iovlen_t>(bufs.size()),
  };

  if (auto rv = ::sendmsg(fd, &msg, flags);
      rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return static_cast<std::size_t>(rv);
}
}  // namespace olifilo::io
