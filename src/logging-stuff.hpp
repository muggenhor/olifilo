// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <string_view>

#include <olifilo/io/types.hpp>

template <typename T, typename Char>
requires std::formattable<T, Char>
struct std::formatter<std::optional<T>, Char>
{
  std::formatter<T, Char> underlying;

  template <typename ParseContext>
  constexpr decltype(auto) parse(ParseContext&& ctx)
  {
    return underlying.parse(std::forward<ParseContext>(ctx));
  }

  template <typename FmtContext>
  auto format(const std::optional<T>& opt, FmtContext&& ctx) const
  {
    auto out = ctx.out();
    if (!opt)
      return std::ranges::copy("none"sv, out).out;

    out = std::ranges::copy("optional("sv, out).out;
    ctx.advance_to(out);
    out = underlying.format(*opt, std::forward<FmtContext>(ctx));
    *out++ = ')';

    return out;
  }
};

template <>
struct std::formatter<olifilo::io::poll_event, char>
{
  template <typename ParseContext>
  constexpr auto parse(ParseContext&& ctx)
  {
    auto it = ctx.begin();

    if (it != ctx.end() && *it != '}')
      throw std::format_error("invalid format args for 'poll_event'");

    return it;
  }

  template <typename FmtContext>
  auto format(olifilo::io::poll_event event, FmtContext&& ctx) const
  {
    using namespace std::literals::string_view_literals;
    using olifilo::io::poll_event;

    auto out = ctx.out();
    if (!std::to_underlying(event))
      return *out++ = '0';
    bool first = true;
    if (std::to_underlying(event & poll_event::read))
    {
      first = false;
      out = std::ranges::copy("read"sv, out).out;
    }
    if (std::to_underlying(event & poll_event::write))
    {
      if (!std::exchange(first, false))
        *out++ = '|';
      out = std::ranges::copy("write"sv, out).out;
    }
    if (std::to_underlying(event & poll_event::priority))
    {
      if (!std::exchange(first, false))
        *out++ = '|';
      out = std::ranges::copy("priority"sv, out).out;
    }
    return out;
  }
};

static inline auto ts() noexcept
{
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}
