// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <olifilo/expected.hpp>

#include <esp_event_base.h>

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
}  // namespace olifilo::esp
