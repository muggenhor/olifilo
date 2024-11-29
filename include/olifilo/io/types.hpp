// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <utility>

namespace olifilo::io
{
using file_descriptor_handle = int;
inline constexpr file_descriptor_handle invalid_file_descriptor_handle = -1;

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
