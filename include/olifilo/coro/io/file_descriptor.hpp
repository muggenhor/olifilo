// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include <unistd.h>

#include "types.hpp"

#include <olifilo/coro/future.hpp>
#include <olifilo/io/types.hpp>

namespace olifilo::io
{
class file_descriptor
{
  public:
    file_descriptor() = default;

    constexpr file_descriptor(io::file_descriptor_handle fd) noexcept
      : _fd(fd)
    {
    }

    virtual ~file_descriptor()
    {
      close();
    }

    file_descriptor(file_descriptor&& rhs) noexcept
      : _fd(rhs.release())
    {
    }

    file_descriptor& operator=(file_descriptor&& rhs) noexcept
    {
      if (&rhs != this)
      {
        close();
        _fd = rhs.release();
      }
      return *this;
    }

    void close() noexcept
    {
      if (_fd)
      {
        ::close(_fd);
        _fd = nullptr;
      }
    }

    constexpr explicit operator bool() const noexcept
    {
      return static_cast<bool>(_fd);
    }

    constexpr io::file_descriptor_handle handle() const noexcept
    {
      return _fd;
    }

    constexpr io::file_descriptor_handle release() noexcept
    {
      return std::exchange(_fd, nullptr);
    }

    future<std::span<std::byte>> read_some(std::span<std::byte> buf, eagerness eager = eagerness::eager) noexcept;
    future<std::span<const std::byte>> write_some(std::span<const std::byte> buf, eagerness eager = eagerness::eager) noexcept;

    future<std::span<std::byte>> read(std::span<std::byte> buf, eagerness eager = eagerness::eager) noexcept;
    future<void> write(std::span<const std::byte> buf, eagerness eager = eagerness::eager) noexcept;

  private:
    io::file_descriptor_handle _fd;
};
}  // olifilo::io
