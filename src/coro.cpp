// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/coro/future.hpp>
#include <olifilo/coro/io/stream_socket.hpp>
#include <olifilo/coro/wait.hpp>
#include <olifilo/coro/when_all.hpp>
#include <olifilo/coro/when_any.hpp>
#include <olifilo/expected.hpp>
#include <olifilo/io/poll.hpp>
#include <olifilo/io/sockopt.hpp>
#include <olifilo/io/sockopts/socket.hpp>
#include <olifilo/io/sockopts/tcp.hpp>
#include <olifilo/io/types.hpp>

#include "logging-stuff.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

#include <arpa/inet.h>
#include <inttypes.h>
#include <netdb.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#include <nvs_handle.hpp>
#else
#include <cstdlib>
#include <cstring>
#endif

namespace olifilo
{
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

    static olifilo::future<mqtt> connect(const char* host, std::uint16_t port, std::uint8_t id) noexcept
    {
#ifdef ESP_PLATFORM
      auto nvs_fs = []() -> olifilo::expected<std::unique_ptr<::nvs::NVSHandle>> {
        esp_err_t err;
        if (auto nvs_fs = ::nvs::open_nvs_handle("olifilo", ::NVS_READONLY, &err); nvs_fs)
          return nvs_fs;
        return {olifilo::unexpect, err, olifilo::esp::error_category()};
      }();
      if (!nvs_fs)
        co_return {olifilo::unexpect, nvs_fs.error()};
#endif

      mqtt con;
      con.keep_alive = decltype(con.keep_alive)(con.keep_alive.count() << (id & 1));
      {
        char portstr[6];
        if (auto status = std::to_chars(std::begin(portstr), std::end(portstr) - 1, port);
            status.ec != std::errc())
          co_return {olifilo::unexpect, std::make_error_code(status.ec)};
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
          co_return {olifilo::unexpect, error, std::generic_category() /* gai/eai::error_category() */};
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
        std::vector<olifilo::future<olifilo::io::stream_socket>> connections;
        for (auto addr = res; addr; addr = addr->ai_next)
          connections.push_back(olifilo::io::stream_socket::create_connection(*addr));

        for (const auto connection_timeout = olifilo::wait_t::clock::now() + con.keep_alive * 2;;)
        {
          assert(!connections.empty());
          auto con_iter = co_await olifilo::wait(olifilo::until::first_completed, connections, connection_timeout);
          if (!con_iter)
            co_return con_iter.error();

          assert(*con_iter != connections.end());
          auto con_task = std::move(**con_iter);
          connections.erase(*con_iter);
          assert(con_task.done() && "task returned from olifilo::wait should be done!");

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
      (void)olifilo::io::setsockopt<olifilo::io::sol_ip_tcp::keep_alive_idle>(con._sock.handle(), con.keep_alive * 2);
      (void)olifilo::io::setsockopt<olifilo::io::sol_socket::keep_alive>(con._sock.handle(), true);

      {
        std::optional<std::string> username, password;
#ifdef ESP_PLATFORM
        {
          std::size_t size;
          if (const auto status = (*nvs_fs)->get_item_size(::nvs::ItemType::SZ, "mqtt.username", size);
              status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND)
            co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
          else if (status == ESP_OK)
            username.emplace(size - 1, 0);

          if (username)
          {
            if (const auto status = (*nvs_fs)->get_string("mqtt.username", &(*username)[0], username->size() + 1);
                status != ESP_OK)
              co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

            if (const auto status = (*nvs_fs)->get_item_size(::nvs::ItemType::SZ, "mqtt.password", size);
                status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND)
            {
              co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
            }
            else if (status == ESP_OK)
            {
              password.emplace(size - 1, 0);
              if (const auto status = (*nvs_fs)->get_string("mqtt.password", &(*password)[0], password->size() + 1);
                  status != ESP_OK)
                co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
            }
          }
        }
#else
        if (const char* const user = std::getenv("MQTT_USERNAME"); user && user[0])
        {
          username.emplace(user);
          if (const char* const pass = std::getenv("MQTT_PASSWORD"); pass)
            password.emplace(pass);
        }
#endif

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
          co_return {olifilo::unexpect, std::make_error_code(status.ec)};
        const std::span<const char> connect_payload_id(
            connect_payload_id_buf, sizeof(connect_payload_id_buf) - 1);
        const std::uint16_t connect_payload_id_len = htons(
            static_cast<std::uint16_t>(connect_payload_id.size()));

        // credentials
        if (username && username->size() > std::numeric_limits<std::uint16_t>::max())
          co_return {olifilo::unexpect, std::make_error_code(std::errc::invalid_argument)};
        if (password && password->size() > std::numeric_limits<std::uint16_t>::max())
          co_return {olifilo::unexpect, std::make_error_code(std::errc::invalid_argument)};
        const std::uint16_t connect_payload_username_len = htons(static_cast<std::uint16_t>(username ? username->size() : 0));
        const std::uint16_t connect_payload_password_len = htons(static_cast<std::uint16_t>(password ? password->size() : 0));

        const std::size_t connect_pkt_size = sizeof(connect_var_header)
          + sizeof(connect_payload_id_len) + connect_payload_id.size()
          + (username ? sizeof(connect_payload_username_len) + username->size() : 0)
          + (password ? sizeof(connect_payload_password_len) + password->size() : 0)
          ;
        if (connect_pkt_size > std::numeric_limits<std::uint32_t>::max())
          co_return {olifilo::unexpect, std::make_error_code(std::errc::message_size)};
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
      auto ack_pkt = co_await con._sock.read(as_writable_bytes(std::span(connack_pkt)), olifilo::eagerness::lazy);
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
      std::uint8_t disconnect_pkt[2] = {
        std::to_underlying(packet_t::disconnect) << 4,
        0,
      };

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
      std::uint8_t ping_pkt[2] = {
        std::to_underlying(packet_t::pingreq) << 4,
        0,
      };

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
    olifilo::io::stream_socket _sock;
};

olifilo::future<void> do_mqtt(std::uint8_t id) noexcept
{
  using namespace std::literals::chrono_literals;

  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({})\n", ts(), __LINE__, "do_mqtt", id);

  std::string mqtt_host("fdce:1234:5678::1");
  std::uint16_t mqtt_port = 1883;
#ifdef ESP_PLATFORM
  if (auto nvs_fs = []() -> olifilo::expected<std::unique_ptr<::nvs::NVSHandle>> {
      esp_err_t err;
      if (auto nvs_fs = ::nvs::open_nvs_handle("olifilo", ::NVS_READONLY, &err); nvs_fs)
        return nvs_fs;
      return {olifilo::unexpect, err, olifilo::esp::error_category()};
    }(); !nvs_fs && nvs_fs.error().value() != ESP_ERR_NVS_NOT_FOUND)
  {
    co_return {olifilo::unexpect, nvs_fs.error()};
  }
  else if (nvs_fs)
  {
    if (const auto status = (*nvs_fs)->get_item("mqtt.port", mqtt_port);
        status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND)
    {
      ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining 'mqtt.port': %s"
          , std::error_code(status, olifilo::esp::error_category()).message().c_str());
      co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }

    ESP_LOGD("olifilo-coro", "do_mqtt: mqtt.port=%" PRIu16, mqtt_port);

    std::size_t size;
    if (const auto status = (*nvs_fs)->get_item_size(::nvs::ItemType::SZ, "mqtt.host", size);
        status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND)
    {
      ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining size of 'mqtt.host': %s"
          , std::error_code(status, olifilo::esp::error_category()).message().c_str());
      co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }
    else if (status == ESP_OK)
    {
      ESP_LOGD("olifilo-coro", "do_mqtt: sizeof(mqtt.host)=%zu", size);
      mqtt_host.resize(size - 1);
      if (const auto status = (*nvs_fs)->get_string("mqtt.host", &mqtt_host[0], mqtt_host.size() + 1);
          status != ESP_OK)
      {
        ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining 'mqtt.host': %s"
            , std::error_code(status, olifilo::esp::error_category()).message().c_str());
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }

      ESP_LOGD("olifilo-coro", "do_mqtt: mqtt.host=\"%s\"", mqtt_host.c_str());
    }
  }
#else
  if (const char* const host = std::getenv("MQTT_HOST"); host && host[0])
    mqtt_host = host;
  if (const char* const port = std::getenv("MQTT_PORT"); port && port[0])
  {
    if (auto status = std::from_chars(port, port + std::strlen(port), mqtt_port);
        status.ec != std::errc())
      co_return {olifilo::unexpect, std::make_error_code(status.ec)};
    else if (status.ptr != port + std::strlen(port)
          || mqtt_port <= 0)
      co_return {olifilo::unexpect, std::make_error_code(std::errc::invalid_argument)};
  }
#endif

  auto r = co_await mqtt::connect(mqtt_host.c_str(), mqtt_port, id);
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

#ifdef ESP_PLATFORM
int mqtt_main()
#else
int main()
#endif
{
  using namespace std::literals::chrono_literals;
  using olifilo::when_all;
  using olifilo::when_any;

#if 0
  if (auto r = when_any(std::array{
        do_mqtt(0),
      }, 30s).get();
      !r)
  {
#if __cpp_exceptions
    throw std::system_error(r.error());
#else
    ESP_LOGE("olifilo-coro", "mqtt_main: error: %s", r.error().message().c_str());
    std::abort();
#endif
  }
  else if (auto ri = r->futures[r->index].get();
      !ri)
  {
#if __cpp_exceptions
    throw std::system_error(ri.error());
#else
    ESP_LOGE("olifilo-coro", "mqtt_main: error(%u): %s", r->index, ri.error().message().c_str());
    std::abort();
#endif
  }
#else
  if (auto [r1, r2, rs] = std::apply(when_all, std::tuple{
        do_mqtt(1)
      , []() -> olifilo::future<void> {
          auto ra = co_await when_any(
              do_mqtt(2)
            , 30s
            );
          if (ra.error() == std::errc::timed_out)
            co_return {};
          else if (!ra)
            co_return {olifilo::unexpect, ra.error()};
          assert(ra->index == 0);
          co_return co_await std::get<0>(ra->futures);
        }()
      , []() -> olifilo::future<std::vector<olifilo::expected<void>>> {
          auto ra = co_await when_any(std::array{
              []() -> olifilo::future<void> {
                auto ra = co_await when_all(std::array{
                  do_mqtt(3),
                  do_mqtt(4),
                });
                if (!ra)
                  co_return {olifilo::unexpect, ra.error()};
                for (auto& ri : *ra)
                  if (!ri)
                    co_return {olifilo::unexpect, ri.error()};

                co_return {};
              }(),
            }, 45s);
          if (ra.error() == std::errc::timed_out)
            co_return {std::in_place};
          else if (!ra)
            co_return {olifilo::unexpect, ra.error()};
          std::vector<olifilo::expected<void>> rv;
          for (auto& fut: ra->futures)
            if (fut.done())
              rv.emplace_back(co_await fut);
          co_return rv;
        }()
      }).get().value();
      !r1 || !r2 || !rs)
  {
    auto err = !r1 ? r1.error() : !r2 ? r2.error() : rs.error();
#if __cpp_exceptions
    throw std::system_error(err);
#else
    ESP_LOGE("olifilo-coro", "mqtt_main: error: %s", err.message().c_str());
    std::abort();
#endif
  }
  else
  {
    for (auto& ri : *rs)
    {
      if (!ri)
      {
#if __cpp_exceptions
        throw std::system_error(ri.error());
#else
        ESP_LOGE("olifilo-coro", "mqtt_main: error ri: %s", ri.error().message().c_str());
        std::abort();
#endif
      }
    }
  }
#endif
  return 0;
}
