// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <cstddef>
#include <span>
#include <system_error>

#include <sys/socket.h>

#include "../expected.hpp"
#include "sockopts/base.hpp"
#include "types.hpp"

namespace olifilo::io
{
inline expected<std::span<std::byte>> getsockopt(file_descriptor_handle fd, int level, int optname, std::span<std::byte> optval) noexcept
{
  ::socklen_t optlen = optval.size_bytes();
  if (auto rv = ::getsockopt(fd, level, optname, optval.data(), &optlen); rv == -1)
    return {unexpect, errno, std::system_category()};
  else
    return {std::in_place, optval.first(optlen)};
}

inline expected<void> setsockopt(file_descriptor_handle fd, int level, int optname, std::span<const std::byte> optval) noexcept
{
  if (auto rv = ::setsockopt(fd, level, optname, optval.data(), optval.size_bytes()); rv == -1)
    return {unexpect, errno, std::system_category()};
  else
    return {};
}

template <detail::SockOptEnum auto optname>
expected<typename detail::socket_opt<optname>::return_type> getsockopt(file_descriptor_handle fd) noexcept
{
  constexpr int level = detail::socket_opt_level<decltype(optname)>::level;
  using opt = detail::socket_opt<optname>;
  typename opt::type optval;
  if (auto rv = getsockopt(fd, level, static_cast<int>(optname), as_writable_bytes(std::span(&optval, 1)));
      !rv)
    return {unexpect, rv.error()};
  else if (rv->size() != sizeof(optval))
    return {unexpect, std::make_error_code(std::errc::invalid_argument)};

  if constexpr (std::is_same_v<typename opt::type, typename opt::return_type>
      || !detail::has_transform_to_semantic<opt>)
    return {std::in_place, optval};
  else
    return {std::in_place, opt::transform(std::move(optval))};
}

template <detail::SockOptEnum auto optname>
expected<void> setsockopt(file_descriptor_handle fd, typename detail::socket_opt<optname>::return_type const val) noexcept
{
  constexpr int level = detail::socket_opt_level<decltype(optname)>::level;
  using opt = detail::socket_opt<optname>;
  const typename opt::type& optval = [&val]() -> decltype(auto) {
    if constexpr (std::is_same_v<typename opt::type, typename opt::return_type>
        || !detail::has_transform_to_native<opt>)
      return val;
    else
      return opt::transform(val);
  }();
  return setsockopt(fd, level, static_cast<int>(optname), as_bytes(std::span(&optval, 1)));
}
}  // namespace olifilo::io
