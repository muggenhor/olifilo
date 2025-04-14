// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <system_error>

namespace olifilo::io
{
enum class mqtt_error
{
  unacceptable_protocol_version = 1, // MQTT 5.0 -> 0xXX
  client_identifier_not_allowed = 2, // MQTT 5.0 -> 0x85
  service_unavailable           = 3, // MQTT 5.0 -> 0x88
  bad_username_or_password      = 4, // MQTT 5.0 -> 0x86
  client_not_authorized         = 5, // MQTT 5.0 -> 0x87
};

struct mqtt_error_category_t : std::error_category
{
  const char* name() const noexcept override;
  std::string message(int ev) const override;
};

constexpr const mqtt_error_category_t& mqtt_error_category() noexcept
{
  static mqtt_error_category_t cat;
  return cat;
}

inline std::error_code make_error_code(mqtt_error e)
{
  return {static_cast<int>(e), mqtt_error_category()};
}
}  // namespace olifilo::io

template <>
struct std::is_error_code_enum<olifilo::io::mqtt_error> : true_type {};
