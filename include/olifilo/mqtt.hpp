// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <olifilo/coro/future.hpp>
#include <olifilo/coro/io/stream_socket.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>

namespace olifilo::io
{
class mqtt
{
  public:
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

    static future<mqtt> connect(
        const char*                     host
      , std::uint16_t                   port
      , std::uint8_t                    id
      , std::optional<std::string_view> username = {}
      , std::optional<std::string_view> password = {}) noexcept;
    future<void> disconnect() noexcept;
    future<void> ping() noexcept;

  private:
    stream_socket _sock;
};
}  // namespace olifilo::io
