// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

#include <olifilo/expected.hpp>
#include <olifilo/coro/future.hpp>
#include <olifilo/coro/io/file_descriptor.hpp>

#include "events/base.hpp"

#include <esp_eth_com.h>
#include <esp_event_base.h>
#include <esp_netif_types.h>
#include <esp_wifi_types_generic.h>

namespace olifilo::esp
{
class event_subscription_default
{
  private:
    ::esp_event_base_t event_base = nullptr;
    std::int32_t event_id = -1;
    ::esp_event_handler_instance_t subscription = nullptr;

    constexpr event_subscription_default(
        ::esp_event_base_t event_base
      , std::int32_t event_id
      , ::esp_event_handler_instance_t subscription
      ) noexcept
      : event_base{event_base}
      , event_id{event_id}
      , subscription{subscription}
    {
    }

    expected<void> destroy() noexcept;

  public:
    event_subscription_default() = default;

    static expected<event_subscription_default> create(
        ::esp_event_base_t event_base
      , std::int32_t event_id
      , ::esp_event_handler_t event_handler
      , void* event_handler_arg
      ) noexcept;

    constexpr event_subscription_default(event_subscription_default&& rhs) noexcept
      : event_base{rhs.event_base}
      , event_id{rhs.event_id}
      , subscription{std::exchange(rhs.subscription, nullptr)}
    {
    }

    constexpr event_subscription_default& operator=(event_subscription_default&& rhs) noexcept
    {
      if (subscription)
        static_cast<void>(destroy());

      this->event_base = rhs.event_base;
      this->event_id = rhs.event_id;
      this->subscription = std::exchange(rhs.subscription, nullptr);
      return *this;
    }

    constexpr ~event_subscription_default()
    {
      if (subscription)
        static_cast<void>(destroy());
    }
};

class event_queue
{
  public:
    using event_id_t = std::variant<
        ::ip_event_t
      , ::eth_event_t
      , ::wifi_event_t
      >;
  private:
    using event_t = std::variant<
        std::monostate
      , ::ip_event_got_ip_t
      , ::wifi_event_sta_connected_t
      , ::wifi_event_sta_disconnected_t
      >;

    std::vector<std::pair<event_id_t, event_t>> events;
    std::mutex event_lock;
    io::file_descriptor notifier;
    std::array<event_subscription_default, std::variant_size_v<event_id_t>> subscriptions;

  public:
    std::error_code init() noexcept;
    event_queue();
    constexpr event_queue(std::nothrow_t) noexcept {} // need to call init() after this constructor!

    future<std::pair<event_id_t, event_t>> receive() noexcept;

  private:
    static void receive(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data) noexcept;
};
}  // namespace olifilo::esp
