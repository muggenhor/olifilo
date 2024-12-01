// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <type_traits>

namespace olifilo::io::detail
{
template <typename T>
concept SockOptEnumBase =
    std::is_enum<T>::value
 && std::is_same_v<std::underlying_type_t<T>, int>;

template <SockOptEnumBase Level>
struct socket_opt_level;

template <typename T>
concept SockOptEnum =
    SockOptEnumBase<T>
 && std::is_same_v<std::remove_cvref_t<decltype(socket_opt_level<T>::level)>, int>;

template <SockOptEnum auto Opt>
struct socket_opt
{
  // Defaulting to 'int' because almost every option is an int
  using type = int;
  using return_type = type;
};
}  // namespace olifilo::io::detail
