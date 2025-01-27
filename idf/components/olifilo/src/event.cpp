// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/idf/event.hpp>

#include <olifilo/idf/errors.hpp>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_vfs_eventfd.h>

namespace olifilo::esp
{
namespace
{
constexpr char TAG[] = "olifilo::esp::event";
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

future<event_queue::event_t> event_queue::receive() noexcept
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
    ESP_LOGD(TAG, "%s: received %lu events", __PRETTY_FUNCTION__, static_cast<std::uint32_t>(event_count));
  }
}

void event_queue::receive(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data) noexcept
{
  auto&& self = *static_cast<event_queue*>(arg);
  std::scoped_lock _(self.event_lock);
  if (event_base == IP_EVENT)
  {
    switch (static_cast<ip_event_t>(event_id))
    {
      case IP_EVENT_STA_GOT_IP:
      case IP_EVENT_ETH_GOT_IP:
      case IP_EVENT_PPP_GOT_IP:
        self.events.emplace_back(std::in_place_type<ip_event_got_ip_t>, *static_cast<const ip_event_got_ip_t*>(event_data));
        break;

      default:
        return; // ignore
    }
  }
  else if (event_base == ETH_EVENT)
  {
    switch (static_cast<eth_event_t>(event_id))
    {
      case ETHERNET_EVENT_CONNECTED:
        self.events.emplace_back(std::in_place_type<eth_event_connected_t>, *static_cast<void* const*>(event_data));
        break;
      case ETHERNET_EVENT_DISCONNECTED:
        self.events.emplace_back(std::in_place_type<eth_event_disconnected_t>, *static_cast<void* const*>(event_data));
        break;

      default:
        return; // ignore
    }
  }
  else if (event_base == WIFI_EVENT)
  {
    switch (static_cast<wifi_event_t>(event_id))
    {
      case WIFI_EVENT_STA_START:
        self.events.emplace_back(std::in_place_type<wifi_event_sta_start_t>);
        break;
      case WIFI_EVENT_STA_CONNECTED:
        self.events.emplace_back(std::in_place_type<wifi_event_sta_connected_t>, *static_cast<const wifi_event_sta_connected_t*>(event_data));
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        self.events.emplace_back(std::in_place_type<wifi_event_sta_disconnected_t>, *static_cast<const wifi_event_sta_disconnected_t*>(event_data));
        break;

      default:
        return; // ignore
    }
  }
  else
  {
    return;
  }

  const std::uint64_t eventnum = 1;
  self.notifier.write(as_bytes(std::span(&eventnum, 1))).get().value();
}
}  // namespace olifilo::esp
