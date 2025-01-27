// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/idf/event.hpp>

#include <olifilo/idf/errors.hpp>

#include <esp_event.h>

namespace olifilo::esp
{
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
}  // namespace olifilo::esp
