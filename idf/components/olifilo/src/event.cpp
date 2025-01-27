// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/idf/event.hpp>

#include <olifilo/idf/errors.hpp>
#include <olifilo/idf/events/eth.hpp>
#include <olifilo/idf/events/ip.hpp>
#include <olifilo/idf/events/wifi.hpp>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_vfs_eventfd.h>

namespace olifilo::esp
{
namespace
{
constexpr char TAG[] = "olifilo::esp::event";

template <typename R, typename T>
  requires(std::is_constructible_v<R, std::in_place_type_t<std::monostate>>)
constexpr R decode_event(void* event_data)
{
  if constexpr (std::is_void_v<T> || !std::is_constructible_v<R, std::in_place_type_t<T>, const T&>)
    return R(std::in_place_type<std::monostate>);
  else
    return R(std::in_place_type<T>, *static_cast<const T*>(event_data));
}

template <typename R, detail::EventIdEnum EventId, std::underlying_type_t<EventId> Base = detail::event_id<EventId>::min>
  requires(std::is_constructible_v<R, std::in_place_type_t<std::monostate>>)
constexpr R decode_event(EventId event_id, void* event_data)
{
  if (std::to_underlying(event_id) < Base)
    return R(std::in_place_type<std::monostate>);

  constexpr auto Max = detail::event_id<EventId>::max;

  switch (std::to_underlying(event_id) - Base)
  {
    case 0:
      if constexpr (0 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(0 + Base)>>(event_data);
    case 1:
      if constexpr (1 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(1 + Base)>>(event_data);
    case 2:
      if constexpr (2 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(2 + Base)>>(event_data);
    case 3:
      if constexpr (3 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(3 + Base)>>(event_data);
    case 4:
      if constexpr (4 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(4 + Base)>>(event_data);
    case 5:
      if constexpr (5 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(5 + Base)>>(event_data);
    case 6:
      if constexpr (6 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(6 + Base)>>(event_data);
    case 7:
      if constexpr (7 + Base <= Max)
        return decode_event<R, detail::event_t<static_cast<EventId>(7 + Base)>>(event_data);
    default:
      if constexpr (Base + 8 <= Max)
        return decode_event<R, EventId, Base + 8>(event_id, event_data);
      else
        return R(std::in_place_type<std::monostate>);
  }
}

template <typename R, std::size_t Base = 0>
  requires(std::tuple_size_v<R> >= 2
        && std::variant_size_v<std::tuple_element_t<0, R>> >= 0
        && std::is_constructible_v<std::tuple_element_t<1, R>, std::in_place_type_t<std::monostate>>)
constexpr expected<R> decode_event(::esp_event_base_t event_base, std::int32_t event_id, void* event_data)
{
  using event_id_t = std::tuple_element_t<0, R>;
  using event_t = std::tuple_element_t<1, R>;
  constexpr auto Max = std::variant_size_v<event_id_t>;

  if constexpr (Base + 0 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 0, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 1 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 1, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 2 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 2, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 3 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 3, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 4 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 4, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 5 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 5, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 6 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 6, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 7 < Max)
  {
    using cur_event_t = std::variant_alternative_t<Base + 7, event_id_t>;
    if (event_base == detail::event_id<cur_event_t>::base)
    {
      const auto event_id_ = static_cast<cur_event_t>(event_id);
      return {std::in_place, event_id_, decode_event<event_t>(event_id_, event_data)};
    }
  }
  if constexpr (Base + 8 < Max)
  {
    return decode_event<R, Base + 8>(event_base, event_id, event_data);
  }

  return {unexpect, make_error_code(std::errc::not_supported)};
}
}  // anonymous namespace

expected<void> event_subscription_default::destroy() noexcept
{
  if (!subscription)
    return {unexpect, make_error_code(std::errc::invalid_argument)};

  return {unexpect, esp_event_handler_instance_unregister(event_base, event_id, subscription), error_category()};
}

expected<event_subscription_default> event_subscription_default::create(
    ::esp_event_base_t event_base
  , std::int32_t event_id
  , ::esp_event_handler_t event_handler
  , void* event_handler_arg
  ) noexcept
{
  ::esp_event_handler_instance_t subscription;
  if (const auto status = ::esp_event_handler_instance_register(
        event_base
      , event_id
      , event_handler
      , event_handler_arg
      , &subscription
      ); status != ESP_OK)
    return {unexpect, status, error_category()};
  return event_subscription_default(event_base, event_id, subscription);
}

std::error_code event_queue::init() noexcept
{
  if (notifier)
    return {};

  // ESP_ERR_INVALID_STATE is returned when a subsystem is already initialized
  if (const auto status = ::esp_event_loop_create_default(); status != ESP_OK && status != ESP_ERR_INVALID_STATE)
    return {status, error_category()};

  {
    constexpr auto config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
    if (const auto status = ::esp_vfs_eventfd_register(&config); status != ESP_OK && status != ESP_ERR_INVALID_STATE)
      return {status, error_category()};
  }

  notifier = io::file_descriptor_handle(eventfd(0, 0));
  if (!notifier)
    return {errno, std::system_category()};
  if (auto subscription = this->subscription.create(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, (esp_event_handler_t)&receive, this); !subscription)
  {
    notifier = nullptr;
    return subscription.error();
  }
  else
  {
    this->subscription = std::move(*subscription);
  }
  return {};
}

event_queue::event_queue()
{
  if (auto error = init())
#if __cpp_exceptions
    throw std::system_error(error);
#else
    std::abort();
#endif
}

future<std::pair<event_queue::event_id_t, event_queue::event_t>> event_queue::receive() noexcept
{
  if (!notifier)
    // used std::nothrow constructor without calling init()?
    co_return {unexpect, make_error_code(std::errc::not_connected)};

  while (true)
  {
    {
      std::scoped_lock _(event_lock);
      if (!events.empty())
      {
        auto rv = events.front();
        events.erase(events.begin());
        co_return rv;
      }
    }

    std::uint64_t event_count;
    if (const auto r = co_await notifier.read(as_writable_bytes(std::span(&event_count, 1)), eagerness::lazy); !r)
      co_return {unexpect, r.error()};
    else if (r->size_bytes() != sizeof(event_count))
    {
      ESP_LOGE(TAG, "event_count size: %u", r->size_bytes());
      co_return {unexpect, ESP_FAIL, error_category()};
    }
    else if (event_count <= 0)
    {
      ESP_LOGE(TAG, "event_count: %llu", event_count);
      co_return {unexpect, ESP_FAIL, error_category()};
    }
    ESP_LOGD(TAG, "received %lu events", static_cast<std::uint32_t>(event_count));
  }
}

void event_queue::receive(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data) noexcept
{
  auto&& self = *static_cast<event_queue*>(arg);
  auto event = decode_event<decltype(self.events)::value_type>(event_base, event_id, event_data);
  ESP_LOGD(TAG, "received event %s:%ld(%p) -> decoded to type %d", event_base, event_id, event_data, event ? static_cast<int>(event->second.index()) : -1);
  if (!event)
    return;

  std::scoped_lock _(self.event_lock);
  self.events.push_back(std::move(*event));
  const std::uint64_t eventnum = 1;
  self.notifier.write(as_bytes(std::span(&eventnum, 1))).get().value();
}
}  // namespace olifilo::esp
