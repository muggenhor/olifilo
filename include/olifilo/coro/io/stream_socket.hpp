// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include "socket_descriptor.hpp"

#include <olifilo/expected.hpp>
#include <olifilo/io/shutdown.hpp>

struct sockaddr;
struct addrinfo;

namespace olifilo::io
{
class stream_socket : public socket_descriptor
{
  public:
    using socket_descriptor::socket_descriptor;

    static expected<stream_socket> create(int domain, int protocol = 0) noexcept;
    static future<stream_socket> create_connection(int domain, int protocol, const ::sockaddr* addr, std::size_t addrlen) noexcept;
    static future<stream_socket> create_connection(const ::addrinfo& addr) noexcept;

    static future<stream_socket> create_connection(
        int domain, const ::sockaddr* addr, std::size_t addrlen) noexcept
    {
      return create_connection(domain, /* protocol=*/ 0, addr, addrlen);
    }

    future<void> connect(const ::sockaddr* addr, std::size_t addrlen) noexcept;
    expected<void> shutdown(io::shutdown_how how) noexcept;

  private:
};
}  // olifilo::io
