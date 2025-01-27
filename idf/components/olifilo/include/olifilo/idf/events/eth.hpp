// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include <esp_eth_com.h>
#include <esp_eth_driver.h>

#include "base.hpp"

namespace olifilo::esp::detail
{
template <>
struct event_id<::eth_event_t>
{
  static constexpr const auto& base = ::ETH_EVENT;
  static constexpr auto min = std::to_underlying(::ETHERNET_EVENT_START);
  static constexpr auto max = std::to_underlying(::ETHERNET_EVENT_DISCONNECTED);
};

template <::eth_event_t EventId>
struct event<EventId>
{
  using type = ::esp_eth_handle_t;
};
}  // namespace olifilo::esp::detail
