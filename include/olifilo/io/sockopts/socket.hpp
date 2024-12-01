// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <system_error>
#include <utility>

#include <sys/socket.h>

#include "base.hpp"

namespace olifilo::io
{
enum class sol_socket : int
{
  accept_connections = SO_ACCEPTCONN,
  broadcast = SO_BROADCAST,
  keep_alive = SO_KEEPALIVE,
  reuse_addr = SO_REUSEADDR,
  type = SO_TYPE, // RAW|STREAM|DGRAM
  error = SO_ERROR,
  receive_buffer_size = SO_RCVBUF,
  send_buffer_size = SO_SNDBUF,
  linger = SO_LINGER,
};

namespace detail
{
template <>
struct socket_opt_level<sol_socket>
{
  static constexpr int level = SOL_SOCKET;
};

template <>
struct socket_opt<sol_socket::error>
{
  static constexpr auto level = socket_opt_level<sol_socket>::level;
  static constexpr int name = std::to_underlying(sol_socket::error);
  using type = int;
  using return_type = std::error_code;

  static constexpr return_type transform(type val) noexcept
  {
    return return_type(val, std::system_category());
  }
};

template <>
struct socket_opt<sol_socket::linger>
{
  static constexpr auto level = socket_opt_level<sol_socket>::level;
  static constexpr auto name = std::to_underlying(sol_socket::linger);
  using type = struct ::linger;
  using return_type = type;
};
}  // namespace detail
}  // namespace olifilo::io
