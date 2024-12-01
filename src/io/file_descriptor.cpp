// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/coro/io/file_descriptor.hpp>
#include <olifilo/io/errors.hpp>
#include <olifilo/io/read.hpp>
#include <olifilo/io/write.hpp>

namespace olifilo::io
{
future<std::span<std::byte>> file_descriptor::read_some(std::span<std::byte> buf, eagerness eager) noexcept
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

future<std::span<const std::byte>> file_descriptor::write_some(std::span<const std::byte> buf, eagerness eager) noexcept
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

future<std::span<std::byte>> file_descriptor::read(std::span<std::byte> const buf, eagerness eager) noexcept
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
    else if (*rv == 0) // HUP/EOF
      co_return buf.first(read_so_far);
    else
      read_so_far += *rv;
  }

  co_return buf;
}

future<void> file_descriptor::write(std::span<const std::byte> buf, eagerness eager) noexcept
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
}  // namespace olifilo::io
