// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <system_error>

#include <sys/socket.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
inline expected<file_descriptor_handle> socket(int domain, int type, int protocol = 0) noexcept
{
  if (file_descriptor_handle rv(::socket(domain, type, protocol)); !rv)
    return std::error_code(errno, std::system_category());
  else
    return rv;
}
}  // namespace olifilo::io
