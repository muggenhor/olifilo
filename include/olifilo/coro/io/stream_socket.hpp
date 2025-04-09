// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include "socket_descriptor.hpp"

#include <olifilo/expected.hpp>
#include <olifilo/io/shutdown.hpp>

struct sockaddr;

namespace olifilo::io
{
class stream_socket : public socket_descriptor
{
  public:
    using socket_descriptor::socket_descriptor;

    static expected<stream_socket> create(int domain, int protocol = 0) noexcept;
    future<void> connect(const ::sockaddr* addr, std::size_t addrlen) noexcept;
    expected<void> shutdown(io::shutdown_how how) noexcept;

  private:
};
}  // olifilo::io
