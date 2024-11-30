// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <cstddef>
#include <span>
#include <system_error>

#include <unistd.h>

#include "../expected.hpp"
#include "types.hpp"

namespace olifilo::io
{
inline expected<std::size_t> write(file_descriptor_handle fd, std::span<const std::byte> buf) noexcept
{
  if (auto rv = ::write(fd, buf.data(), buf.size_bytes());
      rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return static_cast<std::size_t>(rv);
}

inline expected<std::span<const std::byte>> write_some(file_descriptor_handle fd, std::span<const std::byte> buf) noexcept
{
  if (auto rv = write(fd, buf);
      !rv)
    return rv.error();
  else
    return buf.subspan(*rv);
}
}  // namespace olifilo::io
