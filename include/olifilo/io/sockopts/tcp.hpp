// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "base.hpp"

namespace olifilo::io
{
enum class sol_ip_tcp : int
{
#ifdef TCP_FASTOPEN
  fastopen = TCP_FASTOPEN,
#endif
#ifdef TCP_FASTOPEN_CONNECT
  fastopen_connect = TCP_FASTOPEN_CONNECT,
#endif
};

namespace detail
{
template <>
struct socket_opt_level<sol_ip_tcp>
{
  static constexpr int level = IPPROTO_TCP;
};

#ifdef TCP_FASTOPEN_CONNECT
template <>
struct socket_opt<sol_ip_tcp::fastopen_connect>
{
  static constexpr auto level = socket_opt_level<sol_ip_tcp>::level;
  static constexpr auto name = std::to_underlying(sol_ip_tcp::fastopen_connect);
  using type = int;
  using return_type = bool;
};
#endif
}  // namespace detail
}  // namespace olifilo::io
