// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/coro/io/stream_socket.hpp>

#include <olifilo/errors.hpp>
#include <olifilo/io/connect.hpp>
#include <olifilo/io/fcntl.hpp>
#include <olifilo/io/socket.hpp>
#include <olifilo/io/sockopt.hpp>
#include <olifilo/io/sockopts/socket.hpp>

namespace olifilo::io
{
expected<stream_socket> stream_socket::create(int domain, int protocol) noexcept
{
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:.128}\n", ts(), __LINE__, "stream_socket::create");

  constexpr int sock_open_non_block = 0
#if __linux__ || __FreeBSD__ || __NetBSD__ || __OpenBSD__
    // Some OSs allow us to create non-blocking sockets with a single syscall
    | SOCK_NONBLOCK
#endif
  ;

  return io::socket(domain, SOCK_STREAM | sock_open_non_block, protocol)
    .transform([] (auto fd) { return stream_socket(fd); })
    .and_then([] (auto sock) {
      if constexpr (!sock_open_non_block)
        return io::fcntl_get_file_status_flags(sock.handle())
          .and_then([&] (auto flags) { return io::fcntl_set_file_status_flags(sock.handle(), flags | O_NONBLOCK); })
          .transform([&] { return std::move(sock); })
          ;
      else
        return expected<stream_socket>(std::move(sock));
    })
  ;
}

future<void> stream_socket::connect(const ::sockaddr* addr, std::size_t addrlen) noexcept
{
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:.128}\n", ts(), __LINE__, "stream_socket::connect");

  if (addrlen > static_cast<std::size_t>(std::numeric_limits<::socklen_t>::max()))
    co_return make_error_code(std::errc::argument_out_of_domain);

  const auto fd = handle();

  if (auto rv = io::connect(fd, addr, static_cast<::socklen_t>(addrlen));
      rv || rv.error() != condition::operation_not_ready)
    co_return rv;

  if (auto wait = co_await io::poll(handle(), io::poll_event::write); !wait)
    co_return wait;

  if (auto connect_result = io::getsockopt<io::sol_socket::error>(handle());
      !connect_result || *connect_result)
    co_return connect_result ? *connect_result : connect_result.error();

  co_return {};
}

future<stream_socket> stream_socket::create_connection(int domain, int protocol, const ::sockaddr* addr, std::size_t addrlen) noexcept
{
  auto sock = create(domain, protocol);
  if (!sock)
    co_return sock;

  if (auto r = co_await sock->connect(addr, addrlen); !r)
    co_return r.error();

  co_return sock;
}

expected<void> stream_socket::shutdown(io::shutdown_how how) noexcept
{
  return io::shutdown(handle(), how);
}
}  // namespace olifilo::io
