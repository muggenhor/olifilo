// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include <esp_wifi_types_generic.h>

#include "base.hpp"

namespace olifilo::esp::detail
{
template <>
struct event_id<::wifi_event_t>
{
  static constexpr const auto& base = ::WIFI_EVENT;
  static constexpr auto min = std::to_underlying(::WIFI_EVENT_WIFI_READY) + 1; // READY is never generated
  static constexpr auto max = std::to_underlying(::WIFI_EVENT_MAX) - 1;
};

template <>
struct event<::WIFI_EVENT_STA_CONNECTED>
{
  using type = ::wifi_event_sta_connected_t;
};

template <>
struct event<::WIFI_EVENT_STA_DISCONNECTED>
{
  using type = ::wifi_event_sta_disconnected_t;
};

template <>
struct event<::WIFI_EVENT_SCAN_DONE>
{
  using type = ::wifi_event_sta_scan_done_t;
};

template <>
struct event<::WIFI_EVENT_STA_AUTHMODE_CHANGE>
{
  using type = ::wifi_event_sta_authmode_change_t;
};

template <>
struct event<::WIFI_EVENT_STA_WPS_ER_SUCCESS>
{
  using type = ::wifi_event_sta_wps_er_success_t;
};

template <>
struct event<::WIFI_EVENT_STA_WPS_ER_FAILED>
{
  using type = ::wifi_event_sta_wps_fail_reason_t;
};

template <>
struct event<::WIFI_EVENT_STA_WPS_ER_PIN>
{
  using type = ::wifi_event_sta_wps_er_pin_t;
};

template <>
struct event<::WIFI_EVENT_AP_STACONNECTED>
{
  using type = ::wifi_event_ap_staconnected_t;
};

template <>
struct event<::WIFI_EVENT_AP_STADISCONNECTED>
{
  using type = ::wifi_event_ap_stadisconnected_t;
};

template <>
struct event<::WIFI_EVENT_AP_PROBEREQRECVED>
{
  using type = ::wifi_event_ap_probe_req_rx_t;
};

template <>
struct event<::WIFI_EVENT_FTM_REPORT>
{
  using type = ::wifi_event_ftm_report_t;
};

template <>
struct event<::WIFI_EVENT_STA_BSS_RSSI_LOW>
{
  using type = ::wifi_event_bss_rssi_low_t;
};

template <>
struct event<::WIFI_EVENT_ACTION_TX_STATUS>
{
  using type = ::wifi_event_action_tx_status_t;
};

template <>
struct event<::WIFI_EVENT_ROC_DONE>
{
  using type = ::wifi_event_roc_done_t;
};

template <>
struct event<::WIFI_EVENT_AP_WPS_RG_SUCCESS>
{
  using type = ::wifi_event_ap_wps_rg_success_t;
};

template <>
struct event<::WIFI_EVENT_AP_WPS_RG_FAILED>
{
  using type = ::wifi_event_ap_wps_rg_fail_reason_t;
};

template <>
struct event<::WIFI_EVENT_AP_WPS_RG_PIN>
{
  using type = ::wifi_event_ap_wps_rg_pin_t;
};

template <>
struct event<::WIFI_EVENT_NAN_SVC_MATCH>
{
  using type = ::wifi_event_nan_svc_match_t;
};

template <>
struct event<::WIFI_EVENT_NAN_REPLIED>
{
  using type = ::wifi_event_nan_replied_t;
};

template <>
struct event<::WIFI_EVENT_NAN_RECEIVE>
{
  using type = ::wifi_event_nan_receive_t;
};

template <>
struct event<::WIFI_EVENT_NDP_INDICATION>
{
  using type = ::wifi_event_ndp_indication_t;
};

template <>
struct event<::WIFI_EVENT_NDP_CONFIRM>
{
  using type = ::wifi_event_ndp_confirm_t;
};

template <>
struct event<::WIFI_EVENT_NDP_TERMINATED>
{
  using type = ::wifi_event_ndp_terminated_t;
};

template <>
struct event<::WIFI_EVENT_HOME_CHANNEL_CHANGE>
{
  using type = ::wifi_event_home_channel_change_t;
};

template <>
struct event<::WIFI_EVENT_STA_NEIGHBOR_REP>
{
  using type = ::wifi_event_neighbor_report_t;
};
}  // namespace olifilo::esp::detail
