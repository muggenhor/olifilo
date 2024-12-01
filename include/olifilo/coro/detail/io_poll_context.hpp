// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iterator>
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
    template <std::ranges::forward_range R>
    requires(std::is_convertible_v<typename std::iterator_traits<std::ranges::iterator_t<std::remove_cvref_t<R>>>::value_type, awaitable_poll*>)
    std::error_code operator()(R&& polled_events)
    {
      using std::ranges::begin;
      using std::ranges::end;
      using std::ranges::empty;
      using std::ranges::size;
      using std::ranges::iter_swap;
      using std::distance;

      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("run_one"));

      if (empty(polled_events))
        return {};

      fd_set readfds, writefds, exceptfds;
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
      FD_ZERO(&exceptfds);
      unsigned nfds = 0;
      decltype((*begin(polled_events))->timeout) timeout;

      ////const auto now = std::decay_t<decltype(*timeout)>::clock::now();
      ////unsigned idx = 0;
      auto to_resume = end(polled_events);
      for (auto i = begin(polled_events),
            next = (i != to_resume) ? std::next(i) : to_resume;
          i != to_resume;
          i = (next != to_resume) ? next++ : next)
      {
        auto& handler = **i;
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
          if (*handler.timeout < std::decay_t<decltype(*handler.timeout)>::clock::now())
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

        ////idx = 0;
        for (auto i = begin(polled_events),
              next = (i != to_resume) ? std::next(i) : to_resume;
            i != to_resume;
            i = (next != to_resume) ? next++ : next)
        {
          auto& handler = **i;
          ////idx++;

          if (timeout_occurred)
          {
            if (!handler.timeout || now < *handler.timeout)
              continue;

            ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
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
      }

      // Assume that we're the only piece of code *removing* events from the polled_events range.
      // And that every other piece of code pushes to the *back*.
      // Keep indexes instead of iterators because iterators may be invalidated by the event handler queueing a new event.

      // Call handlers from back to front because we've moved the ones ready first to the back
      const auto first_resume = std::distance(begin(polled_events), to_resume);
      auto resume_count = std::distance(to_resume, end(polled_events));
      for (auto i = resume_count; i != 0; --i)
      {
        auto waiter = std::exchange((*std::next(begin(polled_events), first_resume + i - 1))->waiter, nullptr);
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}]resume(waiter={}))\n", ts(), __LINE__, func_name, i, waiter.address());
        if constexpr (std::ranges::random_access_range<std::remove_cvref_t<R>>)
        {
          // pop from the back if we're at the range's back, hopefully reducing the amount of moves necessary later on
          if (size(polled_events) == static_cast<std::size_t>(first_resume + resume_count))
            polled_events.erase(std::next(begin(polled_events), first_resume + --resume_count), end(polled_events));
        }
        waiter.resume();
      }

      // Perform a single sub-ranged erase at the end to have O(n) instead of (n log n)
      to_resume = std::next(begin(polled_events), first_resume);
      polled_events.erase(to_resume, std::next(to_resume, resume_count));

      return {};
    }
};
}  // namespace olifilo::detail
