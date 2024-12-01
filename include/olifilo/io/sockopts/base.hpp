// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <type_traits>
#include <utility>

namespace olifilo::io::detail
{
template <typename T>
concept Enum = std::is_enum<T>::value;

template <Enum Level>
struct socket_opt_level {};

template <Enum auto Opt>
struct socket_opt
{
  static constexpr auto level = socket_opt_level<decltype(Opt)>::level;
  static constexpr auto name = std::to_underlying(Opt);
  // Defaulting to 'int' because almost every option is an int
  using type = int;
  using return_type = type;
};
}  // namespace olifilo::io::detail
