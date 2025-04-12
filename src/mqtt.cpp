// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/mqtt.hpp>

#include <olifilo/coro/wait.hpp>
#include <olifilo/io/sockopt.hpp>
#include <olifilo/io/sockopts/socket.hpp>
#include <olifilo/io/sockopts/tcp.hpp>

#include <iterator>

#include <netdb.h>

namespace olifilo::io
{
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

future<mqtt> mqtt::connect(
    const char*                     host
  , std::uint16_t                   port
  , std::uint8_t                    id
  , std::optional<std::string_view> username
  , std::optional<std::string_view> password) noexcept
{
  mqtt con;
  con.keep_alive = decltype(con.keep_alive)(con.keep_alive.count() << (id & 1));
  {
    char portstr[6];
    if (auto status = std::to_chars(std::begin(portstr), std::end(portstr) - 1, port);
        status.ec != std::errc())
      co_return {unexpect, std::make_error_code(status.ec)};
    else
      *status.ptr = '\0';

    static constexpr ::addrinfo lookup_params{
      .ai_family = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
    };
    ::addrinfo* res = nullptr;
    if (auto error = ::getaddrinfo(host, portstr, &lookup_params, &res);
        error != 0)
    {
      co_return {unexpect, error, std::generic_category() /* gai/eai::error_category() */};
    }
    else if (!res)
    {
      // Huh? getaddrinfo should return an error instead of an empty list!
      co_return std::make_error_code(std::errc::protocol_error);
    }

    struct scope_exit
    {
      ::addrinfo* res;
      ~scope_exit()
      {
        ::freeaddrinfo(res);
      }
    } _(res);

    // Start of Happy Eyeballs (RFC 8305). FIXME: complete me!
    std::vector<future<stream_socket>> connections;
    for (auto addr = res; addr; addr = addr->ai_next)
      connections.push_back(stream_socket::create_connection(*addr));

    for (const auto connection_timeout = wait_t::clock::now() + con.keep_alive * 2;;)
    {
      assert(!connections.empty());
      auto con_iter = co_await wait(until::first_completed, connections, connection_timeout);
      if (!con_iter)
        co_return con_iter.error();

      assert(*con_iter != connections.end());
      auto con_task = std::move(**con_iter);
      connections.erase(*con_iter);
      assert(con_task.done() && "task returned from 'wait' should be done!");

      if (auto con_sock = co_await con_task; con_sock)
      {
        con._sock = std::move(*con_sock);
        break;
      }
      else if (connections.empty())
      {
        co_return con_sock.error();
      }
    }
  }

  // TCP: start sending keep-alive probes after two keep-alive periods have expired without any packets received.
  //      Killing the connection after sol_ip_tcp::keep_alive_count probes have failed to receive a reply.
  (void)setsockopt<sol_ip_tcp::keep_alive_idle>(con._sock.handle(), con.keep_alive * 2);
  (void)setsockopt<sol_socket::keep_alive>(con._sock.handle(), true);

  {
    const std::uint8_t connect_var_header[] = {
      // protocol name
      0,
      4,
      'M',
      'Q',
      'T',
      'T',

      // protocol level
      4,

      // connect flags
      static_cast<std::uint8_t>((username ? 0x80 : 0) /* user name follows */ | (password ? 0x40 : 0) /* password follows */ | 0x02 /* want clean session */),

      // keep alive (seconds, 16 bit big endian)
      static_cast<std::uint8_t>(con.keep_alive.count() >> 8),
      static_cast<std::uint8_t>(con.keep_alive.count()),
    };
    static_assert(sizeof(connect_var_header) == 10);

    // client ID
    char connect_payload_id_buf[14] = "cpp20coromqtt";
    if (auto status = std::to_chars(connect_payload_id_buf + 3, connect_payload_id_buf + 5, 20 + id);
        status.ec != std::errc())
      co_return {unexpect, std::make_error_code(status.ec)};
    const std::span<const char> connect_payload_id(
        connect_payload_id_buf, sizeof(connect_payload_id_buf) - 1);
    const std::uint16_t connect_payload_id_len = htons(
        static_cast<std::uint16_t>(connect_payload_id.size()));

    // credentials
    if (username && username->size() > std::numeric_limits<std::uint16_t>::max())
      co_return {unexpect, std::make_error_code(std::errc::invalid_argument)};
    if (password && password->size() > std::numeric_limits<std::uint16_t>::max())
      co_return {unexpect, std::make_error_code(std::errc::invalid_argument)};
    const std::uint16_t connect_payload_username_len = htons(static_cast<std::uint16_t>(username ? username->size() : 0));
    const std::uint16_t connect_payload_password_len = htons(static_cast<std::uint16_t>(password ? password->size() : 0));

    const std::size_t connect_pkt_size = sizeof(connect_var_header)
      + sizeof(connect_payload_id_len) + connect_payload_id.size()
      + (username ? sizeof(connect_payload_username_len) + username->size() : 0)
      + (password ? sizeof(connect_payload_password_len) + password->size() : 0)
      ;
    if (connect_pkt_size > std::numeric_limits<std::uint32_t>::max())
      co_return {unexpect, std::make_error_code(std::errc::message_size)};
    auto connect_pkt_sizei = static_cast<std::uint32_t>(connect_pkt_size);

    std::uint8_t connect_fixed_header_buf[5] = {
      std::to_underlying(packet_t::connect) << 4,
    };
    size_t connect_fixed_header_len = 1;
    // TODO: extract varint encoding to separate function
    do
    {
      std::uint8_t nibble = connect_pkt_sizei & 0x7f;
      connect_pkt_sizei >>= 7;
      if (connect_pkt_sizei)
        nibble |= 0x80;
      connect_fixed_header_buf[connect_fixed_header_len++] = nibble;
    }
    while (connect_pkt_sizei);
    const std::span connect_fixed_header(connect_fixed_header_buf, connect_fixed_header_len);

    // send CONNECT command
    if (auto r = co_await con._sock.send({
            as_bytes(connect_fixed_header),
            as_bytes(std::span(connect_var_header)),
            as_bytes(std::span(&connect_payload_id_len, 1)),
            as_bytes(connect_payload_id),
            as_bytes(std::span(&connect_payload_username_len, username ? 1 : 0)),
            as_bytes(username ? std::span(*username) : std::span<char>()),
            as_bytes(std::span(&connect_payload_password_len, password ? 1 : 0)),
            as_bytes(password ? std::span(*password) : std::span<char>()),
          });
        !r)
      co_return r.error();
  }

  // expect CONNACK
  std::byte connack_pkt[4] = {};
  auto ack_pkt = co_await con._sock.read(as_writable_bytes(std::span(connack_pkt)), eagerness::lazy);
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

future<void> mqtt::disconnect() noexcept
{
  std::uint8_t disconnect_pkt[2] = {
    std::to_underlying(packet_t::disconnect) << 4,
    0,
  };

  // send DISCONNECT command
  if (auto r = co_await this->_sock.write(as_bytes(std::span(disconnect_pkt)));
      !r)
    co_return r;

  if (auto r = this->_sock.shutdown(shutdown_how::write); !r)
    co_return r;

  if (auto r = co_await this->_sock.read_some(as_writable_bytes(std::span(disconnect_pkt, 1)), eagerness::lazy);
      !r)
    co_return r;
  else if (!r->empty())
    co_return std::make_error_code(std::errc::bad_message);

  this->_sock.close();
  co_return {};
}

future<void> mqtt::ping() noexcept
{
  std::uint8_t ping_pkt[2] = {
    std::to_underlying(packet_t::pingreq) << 4,
    0,
  };

  // send PINGREQ command
  if (auto r = co_await this->_sock.write(as_bytes(std::span(ping_pkt)));
      !r)
    co_return r;

  // expect PINGRESP
  auto ack_pkt = co_await this->_sock.read(as_writable_bytes(std::span(ping_pkt)), eagerness::lazy);
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
}  // namespace olifilo::io
