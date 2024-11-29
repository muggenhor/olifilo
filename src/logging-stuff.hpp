// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <cstring>

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

static inline auto ts() noexcept
{
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}
