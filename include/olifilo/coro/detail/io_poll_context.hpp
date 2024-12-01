// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <unordered_map>

#include <olifilo/expected.hpp>
#include <olifilo/io/poll.hpp>
#include <olifilo/io/select.hpp>
#include <olifilo/io/types.hpp>

#include "promise.hpp"

namespace olifilo::detail
{
// used to register coroutines waiting for events and wait for all those events
class io_poll_context
{
  public:
    io_poll_context() = default;
    io_poll_context(io_poll_context&& rhs) = default;

    constexpr io_poll_context& operator=(io_poll_context&& rhs) noexcept
    {
      if (&rhs == this)
        return *this;

      for (auto& [_, event] : _polled_events)
      {
        event->wait_result = unexpected(std::make_error_code(std::errc::operation_canceled));
        // Don't know if this is necessary. But hopefully better than leaving them dangling or calling destroy()?
        // Not even sure if this is safe in case someone catches the exception this produces...
        event->waiter();
      }

      _polled_events = std::move(rhs._polled_events);

      return *this;
    }

    void wait_for(awaitable_poll& event)
    {
      _polled_events.emplace(event.fd, &event);
    }

    std::error_code run_one()
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("run_one"));

      if (_polled_events.empty())
        return {};

      std::deque<std::coroutine_handle<>> to_resume;
      fd_set readfds, writefds, exceptfds;
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
      FD_ZERO(&exceptfds);
      unsigned nfds = 0;
      decltype(_polled_events.begin()->second->timeout) timeout;

      ////const auto now = std::decay_t<decltype(*timeout)>::clock::now();
      ////unsigned idx = 0;
      for (auto i = _polled_events.begin(),
            last = _polled_events.end(),
            next = (i != last) ? std::next(i) : last;
          i != last;
          i = (next != last) ? next++ : next)
      {
        const auto fd = i->first;
        auto& handler = *i->second;
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx++, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());

        if (handler.fd < 0 && handler.fd >= FD_SETSIZE && !handler.timeout)
        {
          // Because we're using select() which has a very limited range of acceptable file descriptors (usually [0:1024))
          handler.wait_result = unexpected(std::make_error_code(std::errc::bad_file_descriptor));
          to_resume.emplace_back(std::exchange(handler.waiter, nullptr));
          _polled_events.erase(i);
          continue;
        }

        if (handler.timeout)
        {
          if (*handler.timeout < std::decay_t<decltype(*handler.timeout)>::clock::now())
          {
            handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
            to_resume.emplace_back(std::exchange(handler.waiter, nullptr));
            _polled_events.erase(i);
            continue;
          }

          if (timeout)
            timeout = std::min(*timeout, *handler.timeout);
          else
            timeout = *handler.timeout;
        }

        if (!fd)
          continue;

        assert(fd < FD_SETSIZE);
        if (std::to_underlying(handler.events & io::poll_event::read))
        {
          FD_SET(fd, &readfds);
          nfds = std::max(nfds, static_cast<unsigned>(fd + 1));
        }
        if (std::to_underlying(handler.events & io::poll_event::write))
        {
          FD_SET(fd, &writefds);
          nfds = std::max(nfds, static_cast<unsigned>(fd + 1));
        }
        if (std::to_underlying(handler.events & io::poll_event::priority))
        {
          FD_SET(fd, &exceptfds);
          nfds = std::max(nfds, static_cast<unsigned>(fd + 1));
        }
      }

      if (nfds || timeout)
      {
        bool timeout_occurred;
        if (const auto r = io::select(nfds, nfds ? &readfds : nullptr, nfds ? &writefds : nullptr, nfds ? &exceptfds : nullptr, timeout); !r)
          return r.error();
        else
          timeout_occurred = (*r == 0);

        const auto now = timeout
          ? std::decay_t<decltype(*timeout)>::clock::now()
          : std::decay_t<decltype(*timeout)>();

        const auto last = _polled_events.end();
        ////idx = 0;
        for (auto i = _polled_events.begin(),
              next = (i != last) ? std::next(i) : last;
            i != last;
            i = (next != last) ? next++ : next)
        {
          const auto fd = i->first;
          auto& handler = *i->second;
          ////idx++;

          if (timeout_occurred)
          {
            if (!handler.timeout || now < *handler.timeout)
              continue;

            ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
            handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
            to_resume.emplace_back(std::exchange(handler.waiter, nullptr));
            _polled_events.erase(i);

            continue;
          }

          if (!fd)
            continue;

          if (!(std::to_underlying(handler.events & io::poll_event::read    ) && FD_ISSET(fd, &readfds))
           && !(std::to_underlying(handler.events & io::poll_event::write   ) && FD_ISSET(fd, &writefds))
           && !(std::to_underlying(handler.events & io::poll_event::priority) && FD_ISSET(fd, &exceptfds)))
            continue;

          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
          handler.wait_result.emplace(); // no polling error (may be an error event but that's for checking downstream)
          to_resume.emplace_back(std::exchange(handler.waiter, nullptr));
          _polled_events.erase(i);
        }
      }

      ////idx = 0;
      for (auto& waiter: to_resume)
      {
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}]resume(waiter={}))\n", ts(), __LINE__, func_name, idx++, waiter.address());
        waiter.resume();
      }

      return {};
    }

    std::error_code run()
    {
      while (!empty())
      {
        if (auto error = run_one())
          return error;
      }

      return {};
    }

    constexpr bool empty() const noexcept
    {
      return _polled_events.empty();
    }

  private:
    std::unordered_multimap<io::file_descriptor_handle, awaitable_poll*> _polled_events;
};
}  // namespace olifilo::detail
