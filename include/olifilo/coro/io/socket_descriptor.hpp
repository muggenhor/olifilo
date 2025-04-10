// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>

#include "file_descriptor.hpp"

namespace olifilo::io
{
class socket_descriptor : public file_descriptor
{
  public:
    using file_descriptor::file_descriptor;

    future<void> send(
        std::span<const std::span<const std::byte>> bufs
      , eagerness                                   eager = eagerness::eager
      ) noexcept;

    future<void> send(
        std::initializer_list<const std::span<const std::byte>> bufs
      , eagerness                                               eager = eagerness::eager
      ) noexcept
    {
      return send(std::span<const std::span<const std::byte>>(bufs), eager);
    }
};
}  // olifilo::io
