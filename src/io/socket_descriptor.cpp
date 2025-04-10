// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/coro/io/socket_descriptor.hpp>

#include <olifilo/io/sendmsg.hpp>
#include <olifilo/io/write.hpp>

namespace olifilo::io
{
future<void> socket_descriptor::send(
    std::span<const std::span<const std::byte>> bufs
  , eagerness                                   eager
  ) noexcept
{
  const auto fd = handle();

  size_t sent = 0;
  if (eager == eagerness::eager)
  {
    if (auto rv = sendmsg(fd, bufs, MSG_DONTWAIT);
        !rv && rv.error() != condition::operation_not_ready)
      co_return {olifilo::unexpect, rv.error()};
    else
      sent = *rv;
  }

  while (!bufs.empty())
  {
    // remove *wholly* completed buffers
    while (sent >= bufs.front().size())
    {
      sent -= bufs.front().size();
      bufs = bufs.subspan(1);

      if (bufs.empty())
        co_return {};
    }

    if (auto wait = co_await poll(fd, poll::write); !wait)
      co_return wait;

    // transmit partial buffers separately, because we can't modify them instead as they're const
    if (sent)
    {
      if (auto rv = io::write(fd, bufs.front().subspan(sent)); !rv)
        co_return {olifilo::unexpect, rv.error()};
      else
        sent += *rv;
      continue;
    }

    if (auto rv = sendmsg(fd, bufs, MSG_DONTWAIT); !rv)
      co_return {olifilo::unexpect, rv.error()};
    else
      sent += *rv;
  }

  co_return {};
}
}  // namespace olifilo::io
