// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional> // for std::hash
#include <utility>

namespace olifilo::io
{
// non-owning handle that's just a strong-typedef to 'int' with special treatment of -1
class file_descriptor_handle
{
  public:
    file_descriptor_handle() = default;
    file_descriptor_handle(const file_descriptor_handle&) = default;
    file_descriptor_handle(file_descriptor_handle&&) = default;
    file_descriptor_handle& operator=(const file_descriptor_handle&) = default;
    file_descriptor_handle& operator=(file_descriptor_handle&&) = default;

    explicit constexpr file_descriptor_handle(int fd) noexcept
      : _fd(fd)
    {
    }

    constexpr file_descriptor_handle(std::nullptr_t) noexcept
    {
    }

    file_descriptor_handle& operator=(std::nullptr_t) noexcept
    {
      _fd = -1;
      return *this;
    }

    explicit(false) constexpr operator int() const noexcept
    {
      return _fd;
    }

    explicit constexpr operator bool() const noexcept
    {
      return _fd != -1;
    }

  private:
    int _fd = -1;
};

enum class poll_event : std::uint32_t
{
  read     =  0x1,
  priority =  0x2,
  write    =  0x4,
};

constexpr poll_event operator|(poll_event lhs, poll_event rhs) noexcept
{
  return static_cast<poll_event>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr poll_event& operator|=(poll_event& lhs, poll_event rhs) noexcept
{
  return lhs = (lhs | rhs);
}

constexpr poll_event operator&(poll_event lhs, poll_event rhs) noexcept
{
  return static_cast<poll_event>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

constexpr poll_event& operator&=(poll_event& lhs, poll_event rhs) noexcept
{
  return lhs = (lhs & rhs);
}

constexpr poll_event operator~(poll_event e) noexcept
{
  return static_cast<poll_event>(~std::to_underlying(e));
}
}  // namespace olifilo::io

template <>
struct std::hash<olifilo::io::file_descriptor_handle>
{
  constexpr decltype(auto) operator()(olifilo::io::file_descriptor_handle fd) const noexcept
  {
    return std::hash<int>()(static_cast<int>(fd));
  }
};
