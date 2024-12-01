// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/coro/future.hpp>
#include <olifilo/expected.hpp>
#include <olifilo/io/connect.hpp>
#include <olifilo/io/errors.hpp>
#include <olifilo/io/fcntl.hpp>
#include <olifilo/io/poll.hpp>
#include <olifilo/io/read.hpp>
#include <olifilo/io/shutdown.hpp>
#include <olifilo/io/select.hpp>
#include <olifilo/io/socket.hpp>
#include <olifilo/io/sockopt.hpp>
#include <olifilo/io/sockopts/socket.hpp>
#include <olifilo/io/sockopts/tcp.hpp>
#include <olifilo/io/types.hpp>
#include <olifilo/io/write.hpp>

#include "logging-stuff.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

#include <arpa/inet.h>
#include <unistd.h>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

namespace olifilo
{
enum class eagerness
{
  lazy,
  eager,
};

class file_descriptor
{
  public:
    file_descriptor() = default;

    constexpr file_descriptor(io::file_descriptor_handle fd) noexcept
      : _fd(fd)
    {
    }

    virtual ~file_descriptor()
    {
      close();
    }

    file_descriptor(file_descriptor&& rhs) noexcept
      : _fd(rhs.release())
    {
    }

    file_descriptor& operator=(file_descriptor&& rhs) noexcept
    {
      if (&rhs != this)
      {
        close();
        _fd = rhs.release();
      }
      return *this;
    }

    void close() noexcept
    {
      if (_fd)
      {
        ::close(_fd);
        _fd = nullptr;
      }
    }

    constexpr explicit operator bool() const noexcept
    {
      return static_cast<bool>(_fd);
    }

    constexpr io::file_descriptor_handle handle() const noexcept
    {
      return _fd;
    }

    constexpr io::file_descriptor_handle release() noexcept
    {
      return std::exchange(_fd, nullptr);
    }

    future<std::span<std::byte>> read_some(std::span<std::byte> buf, eagerness eager = eagerness::eager) noexcept;
    future<std::span<const std::byte>> write_some(std::span<const std::byte> buf, eagerness eager = eagerness::eager) noexcept;

    future<std::span<std::byte>> read(std::span<std::byte> buf, eagerness eager = eagerness::eager) noexcept;
    future<void> write(std::span<const std::byte> buf, eagerness eager = eagerness::eager) noexcept;

  private:
    io::file_descriptor_handle _fd;
};

template <typename... Ts>
future<std::tuple<expected<Ts>...>> when_all(future<Ts>... futures) noexcept
{
  ////std::string_view func_name(__PRETTY_FUNCTION__);
  ////func_name = func_name.substr(func_name.find("when_all"));

  auto& my_promise = co_await my_current_promise();
  assert(my_promise.events.empty());
  assert(my_promise.root_caller == &my_promise);

  // Force promise.await_transform(await-expr) to be executed for all futures *before* suspending execution of *this* coroutine when invoking co_await.
  // Unfortunately whether the co_await pack expansion executes in this order or once per future just before suspending for each future is implementation-defined. So we need this hack...
  (my_promise.await_transform(std::move(futures)), ...);

  // Now allow this future's .get() to handle the actual I/O multiplexing
  co_return std::tuple<expected<Ts>...>((co_await futures)...);
}

template <std::forward_iterator I, std::sentinel_for<I> S>
requires(is_future_v<typename std::iterator_traits<I>::value_type>)
future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> when_all(I first, S last) noexcept
{
  ////std::string_view func_name(__PRETTY_FUNCTION__);
  ////func_name = func_name.substr(func_name.find("when_all"));

  std::vector<typename std::iterator_traits<I>::value_type::value_type> rv;

  auto& my_promise = co_await my_current_promise();

  // Force promise.await_transform(await-expr) to be executed for all futures *before* suspending execution of *this* coroutine when invoking co_await.
  std::size_t count = 0;
  for (auto i = first; i != last; ++i, ++count)
    *i = my_promise.await_transform(std::move(*i));

  rv.reserve(count);
  // Now allow this future's .get() to handle the actual I/O multiplexing while collecting the results
  for (; first != last; ++first)
    rv.emplace_back(co_await *first);

  co_return rv;
}

template <std::ranges::forward_range R>
requires(std::is_rvalue_reference_v<R&&>)
auto when_all(R&& futures) noexcept
{
  using std::ranges::begin;
  using std::ranges::end;
  return when_all(begin(futures), end(futures));
}

future<void> sleep_until(io::poll::timeout_clock::time_point time) noexcept
{
  ////auto timeout = time - io::poll::timeout_clock::now();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}@{})\n", ts(), __LINE__, "sleep_until", std::chrono::duration_cast<std::chrono::milliseconds>(timeout), std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()));

  if (auto r = co_await io::poll(time);
      !r && r.error() != std::errc::timed_out)
    co_return r;
  else
    co_return {};
}

future<void> sleep(io::poll::timeout_clock::duration time) noexcept
{
  return sleep_until(time + io::poll::timeout_clock::now());
}

inline future<std::span<std::byte>> file_descriptor::read_some(std::span<std::byte> buf, eagerness eager) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::read_some", fd, buf.size());

  if (eager == eagerness::eager)
  {
    if (auto rv = io::read_some(fd, buf);
        rv || rv.error() != io::error::operation_not_ready)
      co_return rv;
  }

  co_return (
      co_await io::poll(fd, io::poll_event::read)
    ).and_then([=] { return io::read_some(fd, buf); });
}

inline future<std::span<const std::byte>> file_descriptor::write_some(std::span<const std::byte> buf, eagerness eager) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::write_some", fd, buf.size());

  if (eager == eagerness::eager)
  {
    if (auto rv = io::write_some(fd, buf);
        rv || rv.error() != io::error::operation_not_ready)
      co_return rv;
  }

  co_return (
      co_await io::poll(fd, io::poll_event::write)
    ).and_then([=] { return io::write_some(fd, buf); });
}

inline future<std::span<std::byte>> file_descriptor::read(std::span<std::byte> const buf, eagerness eager) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::read", fd, buf.size());
  std::size_t read_so_far = 0;

  if (eager == eagerness::eager)
  {
    if (auto rv = io::read(fd, buf);
        !rv && rv.error() != io::error::operation_not_ready)
      co_return rv.error();
    else if (rv)
      read_so_far += *rv;
  }

  while (read_so_far < buf.size())
  {
    if (auto wait = co_await io::poll(fd, io::poll_event::read); !wait)
      co_return wait.error();

    if (auto rv = io::read(fd, buf.subspan(read_so_far)); !rv)
      co_return rv.error();
    else if (*rv == 0)
      co_return buf.first(read_so_far);
    else
      read_so_far += *rv;
  }

  co_return buf;
}

inline future<void> file_descriptor::write(std::span<const std::byte> buf, eagerness eager) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::write", fd, buf.size());

  if (eager == eagerness::eager)
  {
    if (auto rv = io::write_some(fd, buf);
        !rv && rv.error() != io::error::operation_not_ready)
      co_return rv;
    else if (rv)
      buf = *rv;
  }

  while (!buf.empty())
  {
    if (auto wait = co_await io::poll(fd, io::poll_event::write); !wait)
      co_return wait;

    if (auto rv = io::write_some(fd, buf); !rv)
      co_return rv;
    else
      buf = *rv;
  }

  co_return {};
}

class socket_descriptor : public file_descriptor
{
  public:
    using file_descriptor::file_descriptor;
};

class stream_socket : public socket_descriptor
{
  public:
    using socket_descriptor::socket_descriptor;

    static expected<stream_socket> create(int domain, int protocol = 0) noexcept
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

    future<void> connect(const ::sockaddr* addr, std::size_t addrlen) noexcept
    {
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:.128}\n", ts(), __LINE__, "stream_socket::connect");

      const auto fd = handle();

      if (auto rv = io::connect(fd, addr, addrlen);
          rv || rv.error() != io::error::operation_not_ready)
        co_return rv;

      if (auto wait = co_await io::poll(handle(), io::poll_event::write); !wait)
        co_return wait;

      if (auto connect_result = io::getsockopt<io::sol_socket::error>(handle());
          !connect_result || *connect_result)
        co_return connect_result ? *connect_result : connect_result.error();

      co_return {};
    }

    expected<void> shutdown(io::shutdown_how how) noexcept
    {
      return io::shutdown(handle(), how);
    }

  private:
};
}  // namespace olifilo

class mqtt
{
  public:
    template <std::output_iterator<std::byte> Out>
    Out serialize_remaining_length(Out out, std::uint32_t value)
    {
      *out++ = static_cast<std::byte>(value & 0x7f);

      value >>= 7;
      while (value)
      {
        *out++ = static_cast<std::byte>((value & 0x7f) | 0x80);
        value >>= 7;
      }

      return out;
    }

    enum class packet_t : std::uint8_t
    {
      connect     =  1,
      connack     =  2,
      publish     =  3,
      puback      =  4,
      pubrec      =  5,
      pubrel      =  6,
      pubcomp     =  7,
      subscribe   =  8,
      suback      =  9,
      unsubscribe = 10,
      unsuback    = 11,
      pingreq     = 12,
      pingresp    = 13,
      disconnect  = 14,
    };

    std::chrono::duration<std::uint16_t> keep_alive{15};

    static olifilo::future<mqtt> connect(const char* ipv6, uint16_t port, std::uint8_t id) noexcept
    {
      mqtt con;
      {
        sockaddr_in6 addr {
          .sin6_family = AF_INET6,
          .sin6_port = htons(port),
        };
        if (auto r = inet_pton(addr.sin6_family, ipv6, &addr.sin6_addr);
            r == -1)
          co_return std::error_code(errno, std::system_category());
        else if (r == 0)
          co_return std::make_error_code(std::errc::invalid_argument);

        if (auto r = olifilo::stream_socket::create(addr.sin6_family))
          con._sock = std::move(*r);
        else
          co_return r.error();

        if (auto r = co_await con._sock.connect(reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)); !r)
          co_return r.error();
      }

      con.keep_alive = decltype(con.keep_alive)(con.keep_alive.count() << (id & 1));
      // TCP: start sending keep-alive probes after two keep-alive periods have expired without any packets received.
      //      Killing the connection after sol_ip_tcp::keep_alive_count probes have failed to receive a reply.
      olifilo::io::setsockopt<olifilo::io::sol_ip_tcp::keep_alive_idle>(con._sock.handle(), con.keep_alive * 2);
      olifilo::io::setsockopt<olifilo::io::sol_socket::keep_alive>(con._sock.handle(), true);

      std::uint8_t connect_pkt[27];
      connect_pkt[0] = std::to_underlying(packet_t::connect) << 4;
      connect_pkt[1] = 25;

      // protocol name
      connect_pkt[2] = 0;
      connect_pkt[3] = 4;
      connect_pkt[4] = 'M';
      connect_pkt[5] = 'Q';
      connect_pkt[6] = 'T';
      connect_pkt[7] = 'T';

      // protocol level
      connect_pkt[8] = 4;

      // connect flags
      connect_pkt[9] = 0x02 /* want clean session */;

      // keep alive (seconds, 16 bit big endian)
      connect_pkt[10] = con.keep_alive.count() >> 8;
      connect_pkt[11] = con.keep_alive.count() & 0xff;

      // client ID
      connect_pkt[12] = 0;
      connect_pkt[13] = 13;
      connect_pkt[14] = 'c';
      connect_pkt[15] = 'p';
      connect_pkt[16] = 'p';
      connect_pkt[17] = '2' + (id / 10 % 10);
      connect_pkt[18] = '0' + (id % 10);
      connect_pkt[19] = 'c';
      connect_pkt[20] = 'o';
      connect_pkt[21] = 'r';
      connect_pkt[22] = 'o';
      connect_pkt[23] = 'm';
      connect_pkt[24] = 'q';
      connect_pkt[25] = 't';
      connect_pkt[26] = 't';

      // send CONNECT command
      if (auto r = co_await con._sock.write(as_bytes(std::span(connect_pkt)));
          !r)
        co_return r.error();

      std::memset(connect_pkt, 0, sizeof(connect_pkt));

      // expect CONNACK
      auto ack_pkt = co_await con._sock.read(as_writable_bytes(std::span(connect_pkt, 4)), olifilo::eagerness::lazy);
      if (!ack_pkt)
        co_return ack_pkt.error();
      else if (ack_pkt->size() != 4)
        co_return std::make_error_code(std::errc::connection_aborted);

      if (static_cast<packet_t>(static_cast<std::uint8_t>((*ack_pkt)[0]) >> 4) != packet_t::connack) // Check CONNACK message type
        co_return std::make_error_code(std::errc::bad_message);

      if (static_cast<std::uint8_t>((*ack_pkt)[1]) != 2) // variable length header portion must be exactly 2 bytes
        co_return std::make_error_code(std::errc::bad_message);

      if ((static_cast<std::uint8_t>((*ack_pkt)[2]) & 0x01) != 0) // session-present flag must be unset (i.e. we MUST NOT have a server-side session)
        co_return std::make_error_code(std::errc::bad_message);

      const auto connect_return_code = static_cast<std::uint8_t>((*ack_pkt)[3]);
      if (connect_return_code != 0)
        co_return std::error_code(connect_return_code, std::generic_category() /* mqtt::error_category() */);

      co_return con;
    }

    olifilo::future<void> disconnect() noexcept
    {
      char disconnect_pkt[2];
      disconnect_pkt[0] = std::to_underlying(packet_t::disconnect) << 4;
      disconnect_pkt[1] = 0;

      // send DISCONNECT command
      if (auto r = co_await this->_sock.write(as_bytes(std::span(disconnect_pkt)));
          !r)
        co_return r;

      if (auto r = this->_sock.shutdown(olifilo::io::shutdown_how::write); !r)
        co_return r;

      if (auto r = co_await this->_sock.read_some(as_writable_bytes(std::span(disconnect_pkt, 1)), olifilo::eagerness::lazy);
          !r)
        co_return r;
      else if (!r->empty())
        co_return std::make_error_code(std::errc::bad_message);

      this->_sock.close();
      co_return {};
    }

    olifilo::future<void> ping() noexcept
    {
      char ping_pkt[2];
      ping_pkt[0] = std::to_underlying(packet_t::pingreq) << 4;
      ping_pkt[1] = 0;

      // send PINGREQ command
      if (auto r = co_await this->_sock.write(as_bytes(std::span(ping_pkt)));
          !r)
        co_return r;

      // expect PINGRESP
      auto ack_pkt = co_await this->_sock.read(as_writable_bytes(std::span(ping_pkt)), olifilo::eagerness::lazy);
      if (!ack_pkt)
        co_return ack_pkt;
      else if (ack_pkt->size() != 2)
        co_return std::make_error_code(std::errc::connection_aborted);

      if (static_cast<packet_t>(static_cast<std::uint8_t>((*ack_pkt)[0]) >> 4) != packet_t::pingresp) // Check PINGRESP message type
        co_return std::make_error_code(std::errc::bad_message);

      if (static_cast<std::uint8_t>((*ack_pkt)[1]) != 0) // variable length header portion must be empty
        co_return std::make_error_code(std::errc::bad_message);

      co_return {};
    }

  private:
    olifilo::stream_socket _sock;
};

olifilo::future<void> do_mqtt(std::uint8_t id) noexcept
{
  using namespace std::literals::chrono_literals;

  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({})\n", ts(), __LINE__, "do_mqtt", id);

  auto r = co_await mqtt::connect("fdce:1234:5678::1", 1883, id);
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) r = {}\n", ts(), __LINE__, "do_mqtt", id, static_cast<bool>(r));
  if (!r)
    co_return r;

  using clock = olifilo::io::poll::timeout_clock;
  const auto keep_alive_wait_time = std::chrono::duration_cast<clock::duration>(r->keep_alive) * 3 / 4;
  const auto start = clock::now() - ts();
  constexpr auto run_time = 120s;

  clock::time_point now;
  while ((now = clock::now()) - start < run_time)
  {
    const auto sleep_time = keep_alive_wait_time - (now - start) % keep_alive_wait_time;
    auto err = co_await olifilo::sleep_until(now + sleep_time);
    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
    if (!err)
      co_return err;

    if (clock::now() - start >= run_time)
      break;

    err = co_await r->ping();
    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
    if (!err)
      co_return err;
  }

  auto err = co_await r->disconnect();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
  co_return err;
}

int main()
{
  using olifilo::when_all;

#if 0
  if (auto r = do_mqtt(0).get();
      !r)
    throw std::system_error(r.error());
#else
  if (auto [r1, r2, rs] = when_all(
        do_mqtt(1)
      , do_mqtt(2)
      , when_all(std::array{
          do_mqtt(3),
          do_mqtt(4),
        })
      ).get().value();
      !r1 || !r2 || !rs)
  {
    throw std::system_error(!r1 ? r1.error() : !r2 ? r2.error() : rs.error());
  }
  else
  {
    for (auto& ri : *rs)
    {
      if (!ri)
        throw std::system_error(ri.error());
    }
  }
#endif
}
