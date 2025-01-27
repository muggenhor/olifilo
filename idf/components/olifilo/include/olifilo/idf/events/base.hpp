// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <esp_event_base.h>

namespace olifilo::esp::detail
{
template <typename T>
concept EventIdEnumBase =
    std::is_enum<T>::value
 && std::integral<std::underlying_type_t<T>>
 && sizeof(std::underlying_type_t<T>) <= sizeof(std::int32_t);

template <EventIdEnumBase Base>
struct event_id;

template <typename T>
concept EventIdEnum =
    EventIdEnumBase<T>
 && std::is_same_v<std::decay_t<decltype(event_id<T>::base)>, esp_event_base_t>
 && std::is_same_v<std::remove_cvref_t<decltype(event_id<T>::max)>, std::underlying_type_t<T>>
 && (event_id<T>::min >= std::numeric_limits<std::int32_t>::min() || event_id<T>::min >= 0)
 &&  event_id<T>::max <= std::numeric_limits<std::int32_t>::max();

template <EventIdEnum auto EventId>
struct event
{
  // Defaulting to 'void' because there are plenty of events without payload
  using type = int;
};

template <EventIdEnum auto EventId>
using event_t = typename event<EventId>::type;
}  // namespace olifilo::esp::detail
