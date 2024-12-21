// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>

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

  keep_alive_count = TCP_KEEPCNT,
  keep_alive_idle =
#ifdef TCP_KEEPALIVE
    TCP_KEEPALIVE
#else
    TCP_KEEPIDLE
#endif
  ,
  keep_alive_interval = TCP_KEEPINTVL,
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
  using type = int;
  using return_type = bool;
};
#endif

template <>
struct socket_opt<sol_ip_tcp::keep_alive_idle>
{
  using type = int;
  using return_type =
#ifdef TCP_KEEPALIVE
    // lwIP has alias: TCP_KEEPALIVE = TCP_KEEPIDLE * 1000 (just use the highest resolution one)
    std::chrono::duration<int, std::milli>
#else
    std::chrono::duration<int>
#endif
  ;

  static constexpr return_type transform(type val) noexcept
  {
    return return_type(val);
  }

  static constexpr type transform(return_type val) noexcept
  {
    return val.count();
  }
};

template <>
struct socket_opt<sol_ip_tcp::keep_alive_interval>
{
  using type = int;
  using return_type = std::chrono::duration<int>;

  static constexpr return_type transform(type val) noexcept
  {
    return return_type(val);
  }

  static constexpr type transform(return_type val) noexcept
  {
    return val.count();
  }
};
}  // namespace detail
}  // namespace olifilo::io
