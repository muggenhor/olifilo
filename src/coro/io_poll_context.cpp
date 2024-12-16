// SPDX-License-Identifier: GPL-3.0-or-later

#include "olifilo/coro/detail/io_poll_context.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <ranges>
#include <utility>

#include <olifilo/coro/detail/promise.hpp>
#include <olifilo/expected.hpp>
#include <olifilo/io/poll.hpp>
#include <olifilo/io/select.hpp>
#include <olifilo/io/types.hpp>

namespace olifilo::detail
{
namespace
{
expected<unsigned> extract_events(promise_wait_callgraph& polled, ::fd_set& readfds, ::fd_set& writefds, ::fd_set& exceptfds, std::optional<std::chrono::steady_clock::time_point>& timeout, const std::chrono::steady_clock::time_point now) noexcept
{
  unsigned nfds = 0;

  std::error_code error = error::no_io_pending;
  auto to_resume = std::ranges::end(polled.callees);
  for (auto i = std::ranges::begin(polled.callees),
        next = (i != to_resume) ? std::next(i) : to_resume;
      i != to_resume;
      i = (next != to_resume) ? next++ : next)
  {
    const auto r = visit(
        overloaded{
          [&] (promise_wait_callgraph* const callee)
          {
            // Recurse into 
            return extract_events(*callee, readfds, writefds, exceptfds, timeout, now);
          },
          [&i, &next, &to_resume, &readfds, &writefds, &exceptfds, &timeout, now]
          (awaitable_poll* const handlerp) -> expected<unsigned>
          {
            auto& handler = *handlerp;

            assert(!handler.wait_result && handler.wait_result.error() == error::uninitialized && "event with pending result should have been dispatched");

            ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx++, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());

            if (!(0 <= handler.fd && handler.fd < FD_SETSIZE) && !handler.timeout)
            {
              // Because we're using select() which has a very limited range of acceptable file descriptors (usually [0:1024))
              handler.wait_result = unexpected(std::make_error_code(std::errc::bad_file_descriptor));
              std::ranges::iter_swap(i, --to_resume);
              next = i;
              return {std::in_place, 0};
            }

            if (handler.timeout)
            {
              if (*handler.timeout < now)
              {
                handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
                std::ranges::iter_swap(i, --to_resume);
                next = i;
                return {std::in_place, 0};
              }

              if (timeout)
                timeout = std::min(*timeout, *handler.timeout);
              else
                timeout = *handler.timeout;
            }

            if (!handler.fd)
              return {std::in_place, 0};

            unsigned nfds = 0;
            if (std::to_underlying(handler.events & io::poll_event::read))
            {
              FD_SET(handler.fd, &readfds);
              nfds = handler.fd + 1;
            }
            if (std::to_underlying(handler.events & io::poll_event::write))
            {
              FD_SET(handler.fd, &writefds);
              nfds = handler.fd + 1;
            }
            if (std::to_underlying(handler.events & io::poll_event::priority))
            {
              FD_SET(handler.fd, &exceptfds);
              nfds = handler.fd + 1;
            }

            return {std::in_place, nfds};
          },
        }
      , *i);
    if (!r && r.error() != error::no_io_pending)
      return r;
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
  ////unsigned idx = 0;
  auto to_resume = std::ranges::find_if(polled.callees, [] (const auto& callee) {
      return visit(
          overloaded{
            [] (const awaitable_poll* const handler) {
              return handler->wait_result || handler->wait_result.error() != error::uninitialized;
            },
            [] (auto) {
              return false;
            },
          }
        , callee);
    });
  for (auto i = std::ranges::begin(polled.callees),
        next = (i != to_resume) ? std::next(i) : to_resume;
      i != to_resume;
      i = (next != to_resume) ? next++ : next)
  {
    visit(
        overloaded{
          [&] (promise_wait_callgraph* const callee)
          {
            return mark_events(*callee, readfds, writefds, exceptfds, timeout);
          },
          [&i, &next, &to_resume, &readfds, &writefds, &exceptfds, timeout]
          (awaitable_poll* const handlerp)
          {
            auto& handler = *handlerp;
            ////idx++;

            if (timeout)
            {
              if (!handler.timeout || timeout < *handler.timeout)
                return;

              ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - *timeout); }), handler.waiter.address());
              handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
              std::ranges::iter_swap(i, --to_resume);
              next = i;
              return;
            }

            if (!handler.fd)
              return;

            if (!(std::to_underlying(handler.events & io::poll_event::read    ) && FD_ISSET(handler.fd, &readfds))
             && !(std::to_underlying(handler.events & io::poll_event::write   ) && FD_ISSET(handler.fd, &writefds))
             && !(std::to_underlying(handler.events & io::poll_event::priority) && FD_ISSET(handler.fd, &exceptfds)))
              return;

            ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
            handler.wait_result.emplace(); // no polling error (may be an error event but that's for checking downstream)
            std::ranges::iter_swap(i, --to_resume);
            next = i;
          },
        }
      , *i);
  }
}

std::coroutine_handle<> pop_ready_completion_handler(promise_wait_callgraph& polled) noexcept
{
  // recursing into children who's event handlers may cause them to be destroyed!
  // Only the root node is safe from destruction (at worst it's waiting at its final suspend point)
  // So whenever we've executed any event handler we should restart recursion.
  //
  // We solve this by returning the first ready event handler back to the top-caller and letting
  // it call the event handler, then recursing back into the call tree to find the next ready
  // handler.
  //
  // Pop handlers from back to front because we've moved the ones ready first to the back
  for (auto i = std::ranges::rbegin(polled.callees), last = std::ranges::rend(polled.callees); i != last; ++i)
  {
    assert(*i != nullptr);
    if (auto handler = visit(
        overloaded{
          [] (promise_wait_callgraph* const callee)
          {
            return pop_ready_completion_handler(*callee);
          },
          [i = std::next(i).base(), &polled] (awaitable_poll* const handlerp) -> std::coroutine_handle<>
          {
            auto& handler = *handlerp;
            if (handler.wait_result.error() == error::uninitialized)
              return nullptr;

            auto waiter = std::exchange(handler.waits_on_me, nullptr);
            assert(waiter);
            polled.callees.erase(i);
            ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}]resume(waiter={}))\n", ts(), __LINE__, func_name, i, waiter.address());
            return waiter;
          },
        }
      , *i))
      return handler;
  }

  return nullptr;
}
}  // anonymous namespace

std::error_code io_poll_context::operator()(promise_wait_callgraph& polled)
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

  while (auto handler = pop_ready_completion_handler(polled))
    handler();

  return {};
}
}  // namespace olifilo::detail
