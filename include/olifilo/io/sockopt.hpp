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
    return std::error_code(errno, std::system_category());
  else
    return optval.first(optlen);
}

inline expected<void> setsockopt(file_descriptor_handle fd, int level, int optname, std::span<const std::byte> optval) noexcept
{
  if (auto rv = ::setsockopt(fd, level, optname, optval.data(), optval.size_bytes()); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return {};
}

template <detail::Enum auto optname>
expected<typename detail::socket_opt<optname>::return_type> getsockopt(file_descriptor_handle fd) noexcept
{
  using opt = detail::socket_opt<optname>;
  typename opt::type optval;
  if (auto rv = getsockopt(fd, opt::level, opt::name, as_writable_bytes(std::span(&optval, 1)));
      !rv)
    return unexpected(rv.error());
  else if (rv->size() != sizeof(optval))
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  if constexpr (std::is_same_v<typename opt::type, typename opt::return_type>)
    return optval;
  else
    return opt::transform(std::move(optval));
}

template <detail::Enum auto optname>
expected<void> setsockopt(file_descriptor_handle fd, typename detail::socket_opt<optname>::return_type const val) noexcept
{
  using opt = detail::socket_opt<optname>;
  const std::conditional_t<
      std::is_same_v<typename opt::type, typename opt::return_type>
    , const typename opt::type&
    , typename opt::type
    > optval(val);
  return setsockopt(fd, opt::level, opt::name, as_bytes(std::span(&optval, 1)));
}
}  // namespace olifilo::io
