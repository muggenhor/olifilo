// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <ranges>
#include <type_traits>

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
    std::error_code operator()(promise_wait_callgraph& polled)
    {
      fd_set readfds, writefds, exceptfds;
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
      FD_ZERO(&exceptfds);
      unsigned nfds;
      std::optional<std::chrono::steady_clock::time_point> timeout;

      if (auto r = extract_events(polled, readfds, writefds, exceptfds, timeout, std::chrono::steady_clock::now()); !r)
        return r.error();
      else
        nfds = *r;

      if (nfds || timeout)
      {
        if (const auto r = io::select(nfds, nfds ? &readfds : nullptr, nfds ? &writefds : nullptr, nfds ? &exceptfds : nullptr, timeout); !r)
          return r.error();
        else if (*r == 0)
          timeout.emplace(std::chrono::steady_clock::now());
        else
          timeout.reset();

        mark_events(polled, readfds, writefds, exceptfds, timeout);
      }

      dispatch_events(polled);

      return {};
    }

  private:
    expected<unsigned> extract_events(promise_wait_callgraph& polled, ::fd_set& readfds, ::fd_set& writefds, ::fd_set& exceptfds, std::optional<std::chrono::steady_clock::time_point>& timeout, const std::chrono::steady_clock::time_point now) noexcept
    {
      using std::ranges::begin;
      using std::ranges::end;
      using std::ranges::iter_swap;

      unsigned nfds = 0;

      std::error_code error = error::no_io_pending;
      auto to_resume = end(polled.events);
      for (auto i = begin(polled.events),
            next = (i != to_resume) ? std::next(i) : to_resume;
          i != to_resume;
          i = (next != to_resume) ? next++ : next)
      {
        auto& handler = **i;

        assert(!handler.wait_result && handler.wait_result.error() == error::uninitialized && "event with pending result should have been dispatched");
        error.clear();

        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx++, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());

        if (handler.fd < 0 && handler.fd >= FD_SETSIZE && !handler.timeout)
        {
          // Because we're using select() which has a very limited range of acceptable file descriptors (usually [0:1024))
          handler.wait_result = unexpected(std::make_error_code(std::errc::bad_file_descriptor));
          iter_swap(i, --to_resume);
          next = i;
          continue;
        }

        if (handler.timeout)
        {
          if (*handler.timeout < now)
          {
            handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
            iter_swap(i, --to_resume);
            next = i;
            continue;
          }

          if (timeout)
            timeout = std::min(*timeout, *handler.timeout);
          else
            timeout = *handler.timeout;
        }

        if (!handler.fd)
          continue;

        if (std::to_underlying(handler.events & io::poll_event::read))
        {
          FD_SET(handler.fd, &readfds);
          nfds = std::max(nfds, static_cast<unsigned>(handler.fd + 1));
        }
        if (std::to_underlying(handler.events & io::poll_event::write))
        {
          FD_SET(handler.fd, &writefds);
          nfds = std::max(nfds, static_cast<unsigned>(handler.fd + 1));
        }
        if (std::to_underlying(handler.events & io::poll_event::priority))
        {
          FD_SET(handler.fd, &exceptfds);
          nfds = std::max(nfds, static_cast<unsigned>(handler.fd + 1));
        }
      }

      for (auto* const callee : polled.callees)
      {
        if (auto r = extract_events(*callee, readfds, writefds, exceptfds, timeout, now);
            !r && r.error() != error::no_io_pending)
          return r.error();
        else if (r)
        {
          nfds = std::max(nfds, *r);
          error.clear();
        }
      }

      if (error)
        return error;
      return nfds;
    }

    void mark_events(promise_wait_callgraph& polled, const ::fd_set& readfds, const ::fd_set& writefds, const ::fd_set& exceptfds, const std::optional<std::chrono::steady_clock::time_point> timeout) noexcept
    {
      using std::ranges::begin;
      using std::ranges::find_if;
      using std::ranges::iter_swap;

      ////unsigned idx = 0;
      auto to_resume = find_if(polled.events, [] (const auto* const handler) {
          return handler->wait_result || handler->wait_result.error() != error::uninitialized;
        });
      for (auto i = begin(polled.events),
            next = (i != to_resume) ? std::next(i) : to_resume;
          i != to_resume;
          i = (next != to_resume) ? next++ : next)
      {
        auto& handler = **i;
        ////idx++;

        if (timeout)
        {
          if (!handler.timeout || timeout < *handler.timeout)
            continue;

          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - *timeout); }), handler.waiter.address());
          handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
          iter_swap(i, --to_resume);
          next = i;
          continue;
        }

        if (!handler.fd)
          continue;

        if (!(std::to_underlying(handler.events & io::poll_event::read    ) && FD_ISSET(handler.fd, &readfds))
         && !(std::to_underlying(handler.events & io::poll_event::write   ) && FD_ISSET(handler.fd, &writefds))
         && !(std::to_underlying(handler.events & io::poll_event::priority) && FD_ISSET(handler.fd, &exceptfds)))
          continue;

        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
        handler.wait_result.emplace(); // no polling error (may be an error event but that's for checking downstream)
        iter_swap(i, --to_resume);
        next = i;
      }

      for (auto* const callee : polled.callees)
        mark_events(*callee, readfds, writefds, exceptfds, timeout);
    }

    void dispatch_events(promise_wait_callgraph& polled) noexcept
    {
      using std::ranges::begin;
      using std::ranges::end;
      using std::ranges::size;
      using std::ranges::iter_swap;

      // Assume that we're the only piece of code *removing* events from the polled.events range.
      // And that every other piece of code pushes to the *back*.
      // Keep indexes instead of iterators because iterators may be invalidated by the event handler queueing a new event.

      // Call handlers from back to front because we've moved the ones ready first to the back
      constexpr auto ready_to_resume = [] (const auto* const handler) {
        return handler->wait_result || handler->wait_result.error() != error::uninitialized;
      };
      constexpr auto not_ready_to_resume = [=] (const auto* const handler) {
        return !ready_to_resume(handler);
      };
      auto to_resume = std::ranges::find_if(polled.events, ready_to_resume);
      const auto first_resume = std::distance(begin(polled.events), to_resume);
      auto resume_count = std::distance(
          to_resume
        , std::find_if(to_resume, end(polled.events), not_ready_to_resume)
        );
      for (auto i = resume_count; i != 0; --i)
      {
        auto waiter = std::exchange((*std::next(begin(polled.events), first_resume + i - 1))->waiter, nullptr);
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}]resume(waiter={}))\n", ts(), __LINE__, func_name, i, waiter.address());
        if constexpr (std::ranges::random_access_range<std::remove_cvref_t<decltype(polled.events)>>)
        {
          // pop from the back if we're at the range's back, hopefully reducing the amount of moves necessary later on
          if (size(polled.events) == static_cast<std::size_t>(first_resume + resume_count))
            polled.events.erase(std::next(begin(polled.events), first_resume + --resume_count));
        }
        waiter.resume();
      }

      // Perform a single sub-ranged erase at the end to have O(n) instead of (n log n)
      to_resume = std::next(begin(polled.events), first_resume);
      polled.events.erase(to_resume, std::next(to_resume, resume_count));

      for (auto* const callee : polled.callees)
        dispatch_events(*callee);
    }
};
}  // namespace olifilo::detail
