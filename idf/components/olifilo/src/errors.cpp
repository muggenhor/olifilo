// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/idf/errors.hpp>

#include <esp_err.h>
#include <esp_netif_types.h>
#include <esp_wifi.h>
#include <nvs.h>

namespace olifilo::esp
{
const char* error_category_t::name() const noexcept
{
  return "esp-error";
}

std::string error_category_t::message(int ev) const
{
  return esp_err_to_name(ev);
}

bool error_category_t::equivalent(int code, const std::error_condition& condition) const noexcept
{
  switch (static_cast<::esp_err_t>(code))
  {
    case ESP_ERR_NO_MEM:
      return condition == std::errc::not_enough_memory;
    case ESP_ERR_INVALID_ARG:
      return condition == std::errc::invalid_argument;
    case ESP_ERR_NOT_FOUND:
      return condition == std::errc::no_such_file_or_directory
          || condition == std::errc::no_such_device_or_address
          || condition == std::errc::no_such_device;
    case ESP_ERR_NOT_SUPPORTED:
      return condition == std::errc::not_supported;
    case ESP_ERR_TIMEOUT:
      return condition == std::errc::timed_out;
    case ESP_ERR_INVALID_MAC:
      return condition == std::errc::bad_address;
    case ESP_ERR_NOT_FINISHED:
      return condition == std::errc::operation_in_progress
          || condition == std::errc::operation_would_block;
    case ESP_ERR_NOT_ALLOWED:
      return condition == std::errc::permission_denied;

    case ESP_ERR_WIFI_TIMEOUT:
      return condition == std::errc::timed_out;
    case ESP_ERR_WIFI_WOULD_BLOCK:
      return condition == std::errc::operation_would_block;
    case ESP_ERR_WIFI_NOT_CONNECT:
      return condition == std::errc::not_connected;

    case ESP_ERR_ESP_NETIF_INVALID_PARAMS:
      return condition == std::errc::invalid_argument;
    case ESP_ERR_ESP_NETIF_NO_MEM:
      return condition == std::errc::not_enough_memory;

    case ESP_ERR_NVS_NOT_FOUND:
      return condition == std::errc::no_such_file_or_directory;
    case ESP_ERR_NVS_READ_ONLY:
      return condition == std::errc::read_only_file_system;
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
      return condition == std::errc::no_space_on_device;
    case ESP_ERR_NVS_INVALID_HANDLE:
      return condition == std::errc::bad_file_descriptor;
    case ESP_ERR_NVS_KEY_TOO_LONG:
      return condition == std::errc::filename_too_long;
    case ESP_ERR_NVS_PAGE_FULL:
      return condition == std::errc::no_space_on_device;
    case ESP_ERR_NVS_INVALID_STATE:
      return condition == std::errc::io_error;
    case ESP_ERR_NVS_INVALID_LENGTH:
      return condition == std::errc::no_buffer_space;
    case ESP_ERR_NVS_NO_FREE_PAGES:
      return condition == std::errc::io_error;
    case ESP_ERR_NVS_VALUE_TOO_LONG:
      return condition == std::errc::file_too_large;
    case ESP_ERR_NVS_PART_NOT_FOUND:
      return condition == std::errc::no_such_device;

    case ESP_ERR_NVS_NEW_VERSION_FOUND:
      return condition == std::errc::io_error;
  }

  return false;
}
}  // namespace olifilo::esp
