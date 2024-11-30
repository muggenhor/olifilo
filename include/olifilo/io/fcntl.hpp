// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <system_error>

#include <fcntl.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
template <typename... Args>
expected<int> fcntl(file_descriptor_handle fd, int cmd, Args&&... args) noexcept
{
  if (auto rv = ::fcntl(fd, cmd, std::forward<Args>(args)...); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return rv;
}

inline expected<int> fcntl_get_file_status_flags(file_descriptor_handle fd) noexcept
{
  return fcntl(fd, F_GETFL);
}

inline expected<void> fcntl_set_file_status_flags(file_descriptor_handle fd, int flags) noexcept
{
  return fcntl(fd, F_SETFL, flags);
}
}  // namespace olifilo::io
