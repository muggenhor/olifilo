// SPDX-License-Identifier: GPL-3.0-or-later

// Mostly this file contains stuff I think is missing from <utility>

#pragma once

namespace olifilo
{
template <typename T, typename U>
constexpr auto cv_like(U* const p) noexcept
{
  constexpr bool is_const = std::is_const_v<std::remove_reference_t<T>>;
  constexpr bool is_volatile = std::is_volatile_v<std::remove_reference_t<T>>;
  if constexpr (is_const)
  {
    if constexpr (is_volatile)
      return static_cast<const volatile U*>(p);
    else
      return static_cast<const U*>(p);
  }
  else
  {
    if constexpr (is_volatile)
      return static_cast<volatile U*>(p);
    else
      return p;
  }
}

template <typename T, typename U>
using cv_like_t = decltype(cv_like<T>(std::declval<U>()));
}  // namespace olifilo
