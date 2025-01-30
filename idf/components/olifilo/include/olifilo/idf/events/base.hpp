// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <esp_event_base.h>

namespace olifilo::esp::detail
{
template <typename T>
concept EventIdEnumBase =
    std::is_enum<T>::value
 && std::integral<std::underlying_type_t<T>>
 && sizeof(std::underlying_type_t<T>) <= sizeof(std::int32_t);

template <EventIdEnumBase Base>
struct event_id;

template <typename T>
concept EventIdEnum =
    EventIdEnumBase<T>
 && std::is_same_v<std::decay_t<decltype(event_id<T>::base)>, esp_event_base_t>
 && std::is_same_v<std::decay_t<decltype(event_id<T>::sort_key)>, std::size_t>
 && std::is_same_v<std::remove_cvref_t<decltype(event_id<T>::max)>, std::underlying_type_t<T>>
 && (event_id<T>::min >= std::numeric_limits<std::int32_t>::min() || event_id<T>::min >= 0)
 &&  event_id<T>::max <= std::numeric_limits<std::int32_t>::max();

template <EventIdEnum auto EventId>
struct event
{
  // Defaulting to 'void' because there are plenty of events without payload
  using type = void;
};

template <EventIdEnum auto EventId>
using event_t = typename event<EventId>::type;

// Arbitrary order of events and the sort keys they have as a result.
// The only requirement for sort keys is that they need to be unique.
// These determine the order of EventIdEnum's in parameter packs and thus determine the ABI.
//  0. WIFI_EVENT
//  1. ETH_EVENT
//  2. MESH_EVENT
//  3. IP_EVENT
//  4. SC_EVENT
//  5. NETIF_PPP_STATUS
//  6. OPENTHREAD_EVENT
//  7. WIFI_PROV_EVENT
//  8. WIFI_PROV_MGR_PVT_EVENT
//  9. PROTOCOMM_SECURITY_SESSION_EVENT
// 10. PROTOCOMM_TRANSPORT_BLE_EVENT
// 11. MQTT_EVENTS
// 12. ESP_HIDD_EVENTS
// 13. ESP_HIDH_EVENTS
// 14. ESP_HTTPS_OTA_EVENT
// 15. ESP_HTTPS_SERVER_EVENT
// 16. ESP_HTTP_CLIENT_EVENT
// 17. ESP_HTTP_SERVER_EVENT
}  // namespace olifilo::esp::detail
