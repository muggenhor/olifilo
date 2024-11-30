// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <system_error>
#include <utility>

#include <sys/socket.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
enum class shutdown_how : int
{
  read = SHUT_RD,
  write = SHUT_WR,
  read_write = SHUT_RDWR,
};

inline expected<void> shutdown(file_descriptor_handle fd, shutdown_how how) noexcept
{
  if (auto rv = ::shutdown(fd, std::to_underlying(how)); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return {};
}
}  // namespace olifilo::io
