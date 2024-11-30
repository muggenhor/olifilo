// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <system_error>

#include <sys/socket.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
inline expected<void> connect(file_descriptor_handle fd, const struct ::sockaddr* addr, ::socklen_t addrlen) noexcept
{
  if (auto rv = ::connect(fd, addr, addrlen); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return {};
}
}  // namespace olifilo::io
