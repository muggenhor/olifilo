// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include <esp_netif_types.h>

#include "base.hpp"

namespace olifilo::esp::detail
{
template <>
struct event_id<::ip_event_t>
{
  static constexpr const auto& base = ::IP_EVENT;
  static constexpr auto min = std::to_underlying(::IP_EVENT_STA_GOT_IP);
  static constexpr auto max = std::to_underlying(::IP_EVENT_TX_RX);
};

template <>
struct event<::IP_EVENT_STA_GOT_IP>
{
  using type = ::ip_event_got_ip_t;
};

template <>
struct event<::IP_EVENT_ETH_GOT_IP>
{
  using type = ::ip_event_got_ip_t;
};

template <>
struct event<::IP_EVENT_PPP_GOT_IP>
{
  using type = ::ip_event_got_ip_t;
};

template <>
struct event<::IP_EVENT_GOT_IP6>
{
  using type = ::ip_event_got_ip6_t;
};

template <>
struct event<::IP_EVENT_AP_STAIPASSIGNED>
{
  using type = ::ip_event_ap_staipassigned_t;
};

template <>
struct event<::IP_EVENT_TX_RX>
{
  using type = ::esp_netif_tx_rx_direction_t;
};
}  // namespace olifilo::esp::detail
