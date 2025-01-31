// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <variant>

namespace olifilo::esp::detail
{
template <template <typename...> class Pack, typename MaybePack, typename UniquePack = Pack<>>
struct unique_pack;

template <template <typename...> class Pack, typename... Uniques>
struct unique_pack<Pack, Pack<>, Pack<Uniques...>>
{
  using type = Pack<Uniques...>;
};

template <template <typename...> class Pack, typename Maybe, typename... Maybes, typename... Uniques>
struct unique_pack<Pack, Pack<Maybe, Maybes...>, Pack<Uniques...>>
    : std::conditional_t<
        (!std::is_same_v<Maybe, Uniques> && ...)
      , unique_pack<Pack, Pack<Maybes...>, Pack<Uniques..., Maybe>>
      , unique_pack<Pack, Pack<Maybes...>, Pack<Uniques...>>
> {};

template <typename T>
struct unique;

template <typename T>
using unique_t = typename unique<T>::type;

template <typename... Ts>
struct unique<std::tuple<Ts...>> : unique_pack<std::tuple, std::tuple<Ts...>> {};

template <typename... Ts>
struct unique<std::variant<Ts...>> : unique_pack<std::variant, std::variant<Ts...>> {};

template <typename T>
constexpr std::size_t size_of = sizeof(T);

// helper to avoid expression sizeof(void) which is meaningless
template <>
constexpr std::size_t size_of<void> = 0;

template <EventIdEnum auto... EventIds>
consteval auto sort_indices_impl() noexcept
{
  static constexpr std::array sort_keys{
    std::tuple(event_id<decltype(EventIds)>::sort_key, static_cast<std::int32_t>(EventIds))...
  };
  static constexpr auto to_sort_key = [] (const std::size_t i) { return sort_keys[i]; };

  std::array<std::size_t, sizeof...(EventIds)> indices;
  std::ranges::copy(std::views::iota(std::size_t(0), indices.size()), indices.begin());

  std::ranges::sort(indices, {}, to_sort_key);
  auto non_unique = std::ranges::unique(indices, {}, to_sort_key);

  return std::tuple(indices, non_unique.begin() - indices.begin());
}

template <EventIdEnum auto... EventIds>
consteval auto sort_indices() noexcept
{
  constexpr auto indices_and_end = sort_indices_impl<EventIds...>();
  constexpr auto indices = std::get<0>(indices_and_end);
  constexpr auto unique_size = std::get<1>(indices_and_end);
  std::array<std::size_t, unique_size> unique_indices;
  std::copy_n(indices.begin(), unique_size, unique_indices.begin());
  return unique_indices;
}

template <typename... Ts>
concept contains_void = (std::is_void_v<Ts> || ...);

template <typename T, typename MatchingTuple, typename MaybeTuple>
struct filter_type;

template <typename T, T... Matching>
struct filter_type<
    T
  , std::tuple<std::integral_constant<T, Matching>...>
  , std::tuple<>
  >
{
  using type = std::tuple<std::integral_constant<T, Matching>...>;
};

template <typename T, T... Matching, auto Maybe, auto... Maybes>
struct filter_type<
    T
  , std::tuple<std::integral_constant<T, Matching>...>
  , std::tuple<
      std::integral_constant<decltype(Maybe), Maybe>
    , std::integral_constant<decltype(Maybes), Maybes>...
    >
  >
  : std::conditional_t<
      std::is_same_v<decltype(Maybe), T>
    , filter_type<
        T
      , std::tuple<std::integral_constant<T, Matching>..., std::integral_constant<T, static_cast<T>(Maybe)>>
      , std::tuple<std::integral_constant<decltype(Maybes), Maybes>...>
      >
    , filter_type<
        T
      , std::tuple<std::integral_constant<T, Matching>...>
      , std::tuple<std::integral_constant<decltype(Maybes), Maybes>...>
      >
    > {};

template <typename T, auto... Maybes>
using filter_type_t = typename filter_type<T, std::tuple<>, std::tuple<std::integral_constant<decltype(Maybes), Maybes>...>>::type;

template <typename R, typename T>
constexpr expected<R> decode_event_data(std::span<const std::byte> event_data)
{
  if constexpr (std::is_void_v<R>)
  {
    static_assert(std::is_void_v<T>);
    return {std::in_place};
  }
  else if constexpr (std::is_void_v<T>)
  {
    if constexpr (std::is_constructible_v<R, std::in_place_type_t<std::monostate>>)
      return {std::in_place, std::in_place_type<std::monostate>};
    else
      return {std::in_place};
  }
  else
  {
    // truncated
    if (event_data.size() < sizeof(T))
      return {unexpect, make_error_code(std::errc::no_buffer_space)};

    const T& data = *static_cast<const T*>(static_cast<const void*>(event_data.data()));
    if constexpr (std::is_constructible_v<R, std::in_place_type_t<T>, const T&>)
      return {std::in_place, std::in_place_type<T>, data};
    else
      return {std::in_place, data};
  }
}

template <typename R, typename EventIds, std::size_t Base = 0, EventIdEnum EventId>
#if __GNUC__
// Causes significant code reduction for large enums
__attribute__((__always_inline__))
#endif
constexpr expected<R> decode_event_for_base(EventId id, std::span<const std::byte> event_data) noexcept
{
  constexpr auto Max = std::tuple_size_v<EventIds>;

  if constexpr (Base + 0 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 0, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 1 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 1, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 2 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 2, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 3 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 3, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 4 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 4, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 5 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 5, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 6 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 6, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 7 < Max)
  {
    constexpr EventId cur_event = std::tuple_element_t<Base + 7, EventIds>::value;
    if (id == cur_event)
      return decode_event_data<R, detail::event_t<cur_event>>(event_data);
  }
  if constexpr (Base + 8 < Max)
  {
    return decode_event_data<R, EventIds, Base + 8>(id, event_data);
  }

  return {unexpect, make_error_code(std::errc::bad_message)};
}

template <typename R, typename Event, EventIdEnum EventId, detail::EventIdEnum auto... EventIds>
#if __GNUC__
// Causes significant code reduction for large enums
__attribute__((__always_inline__))
#endif
constexpr expected<R> decode_event_for_base(std::int32_t id, std::span<const std::byte> event_data) noexcept
{
  const auto event_id = static_cast<EventId>(id);
  auto data = decode_event_for_base<Event, unique_t<filter_type_t<EventId, EventIds...>>>(
      event_id
    , event_data
    );
  if (!data)
    return {unexpect, data.error()};

  if constexpr (std::is_same_v<R, Event>)
  {
    if constexpr (std::is_void_v<Event>)
      return {std::in_place};
    else
      return {std::in_place, *std::move(data)};
  }
  else
  {
    if constexpr (std::is_void_v<Event>)
      return {std::in_place, event_id};
    else
      return {std::in_place, event_id, *std::move(data)};
  }
}

template <typename R, typename Event, typename var_event_id_t, std::size_t Base, detail::EventIdEnum auto... EventIds>
constexpr expected<R> decode_event(::esp_event_base_t base, std::int32_t id, std::span<const std::byte> event_data) noexcept
{
  constexpr auto Max = std::variant_size_v<var_event_id_t>;

  if constexpr (Base + 0 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 0, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 1 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 1, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 2 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 2, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 3 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 3, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 4 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 4, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 5 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 5, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 6 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 6, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 7 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 7, var_event_id_t>;
    if (base == detail::event_id<cur_event_t>::base)
      return detail::decode_event_for_base<R, Event, cur_event_t, EventIds...>(id, event_data);
  }
  if constexpr (Base + 8 < Max)
  {
    return decode_event<Base + 8>(base, id, event_data);
  }

  return {unexpect, make_error_code(std::errc::bad_message)};
}
}  // namespace olifilo::esp::detail
