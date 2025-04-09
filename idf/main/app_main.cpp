#include <bit>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include <esp_err.h>
#include <esp_eth.h>
#include <esp_eth_mac.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <nvs_handle.hpp>
#include <soc/soc.h>

#include <olifilo/idf/errors.hpp>
#include <olifilo/idf/event.hpp>
#include <olifilo/idf/events/eth.hpp>
#include <olifilo/idf/events/ip.hpp>
#include <olifilo/idf/events/wifi.hpp>

#include "../../src/coro.cpp"

static constexpr char TAG[] = "olifilo-test";
namespace
{
template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
}  // anonymous namespace

namespace olifilo::esp
{
future<void> wifi_start() noexcept
{
  auto started_event = olifilo::esp::events::subscribe<WIFI_EVENT_STA_START>();
  if (!started_event)
    co_return {olifilo::unexpect, started_event.error()};

  if (const auto status = ::esp_wifi_start(); status != ESP_OK)
    co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

  co_return co_await started_event->receive();
}
}  // namespace olifilo::esp

namespace esp
{
struct eth_deleter
{
  static void operator()(esp_netif_t* netif) noexcept
  {
    ::esp_netif_destroy(netif);
  }

  static olifilo::expected<void> operator()(esp_eth_handle_t handle) noexcept
  {
    if (const auto status = ::esp_eth_stop(handle); status != ESP_OK)
      return {olifilo::unexpect, status, olifilo::esp::error_category()};
    if (const auto status = ::esp_eth_driver_uninstall(handle); status != ESP_OK)
      return {olifilo::unexpect, status, olifilo::esp::error_category()};
    return {};
  }

  static olifilo::expected<void> operator()(esp_eth_netif_glue_handle_t glue) noexcept
  {
    if (const auto status = ::esp_eth_del_netif_glue(glue); status != ESP_OK)
      return {olifilo::unexpect, status, olifilo::esp::error_category()};
    return {};
  }

  static olifilo::expected<void> operator()(esp_eth_mac_t* mac) noexcept
  {
    if (!mac)
      return {olifilo::unexpect, make_error_code(std::errc::invalid_argument)};
    if (const auto status = mac->del(mac); status != ESP_OK)
      return {olifilo::unexpect, status, olifilo::esp::error_category()};
    return {};
  }

  static olifilo::expected<void> operator()(esp_eth_phy_t* phy) noexcept
  {
    if (!phy)
      return {olifilo::unexpect, make_error_code(std::errc::invalid_argument)};
    if (const auto status = phy->del(phy); status != ESP_OK)
      return {olifilo::unexpect, status, olifilo::esp::error_category()};
    return {};
  }
};

struct networking
{
  std::unique_ptr<esp_netif_t, esp::eth_deleter> netif;
#if CONFIG_ETH_USE_OPENETH
  std::unique_ptr<esp_eth_mac_t, esp::eth_deleter> mac;
  std::unique_ptr<esp_eth_phy_t, esp::eth_deleter> phy;
  std::unique_ptr<std::remove_pointer_t<esp_eth_handle_t>, esp::eth_deleter> eth_handle;
  std::unique_ptr<std::remove_pointer_t<esp_eth_netif_glue_handle_t>, esp::eth_deleter> eth_glue;
#endif

  static olifilo::future<networking> create() noexcept
  {
    if (auto status = olifilo::esp::events::init(); !status)
      co_return {olifilo::unexpect, status.error()};

    if (const auto status = ::esp_netif_init(); status != ESP_OK)
      co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

    networking network;

#if CONFIG_ETH_USE_OPENETH
#ifndef OPENETH_MODER_REG
#  define OPENETH_MODER_REG           0x600CD000
#endif
#ifndef OPENETH_MODER_DEFAULT
#  define OPENETH_MODER_DEFAULT       0xa000
#endif

    if ([] {
        const auto regval = REG_READ(OPENETH_MODER_REG);
        ESP_LOGD(TAG, "REG_READ(OPENETH_MODER_REG) (%#lx) == OPENETH_MODER_DEFAULT (%#x)", regval, OPENETH_MODER_DEFAULT);
        return regval == OPENETH_MODER_DEFAULT;
      }())
    {
      {
        esp_netif_inherent_config_t base_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
        base_config.if_desc = "openeth";
        base_config.route_prio = 64;
        esp_netif_config_t config = {
            .base = &base_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        network.netif.reset(esp_netif_new(&config));
        if (!network.netif)
          co_return {olifilo::unexpect, ESP_FAIL, olifilo::esp::error_category()};
      }

      {
        eth_mac_config_t config = ETH_MAC_DEFAULT_CONFIG();
        config.rx_task_stack_size = 2048;
        network.mac.reset(esp_eth_mac_new_openeth(&config));
        if (!network.mac)
          co_return {olifilo::unexpect, ESP_FAIL, olifilo::esp::error_category()};
      }

      {
        eth_phy_config_t config = ETH_PHY_DEFAULT_CONFIG();
        config.phy_addr = 1;
        config.reset_gpio_num = 5;
        config.autonego_timeout_ms = 100;
        network.phy.reset(esp_eth_phy_new_dp83848(&config));
        if (!network.phy)
          co_return {olifilo::unexpect, ESP_FAIL, olifilo::esp::error_category()};
      }

      // Install Ethernet driver
      {
        esp_eth_config_t config = ETH_DEFAULT_CONFIG(network.mac.get(), network.phy.get());
        esp_eth_handle_t handle;
        if (const auto status = ::esp_eth_driver_install(&config, &handle); status != ESP_OK)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
        network.eth_handle.reset(handle);
      }

      // combine driver with netif
      network.eth_glue.reset(esp_eth_new_netif_glue(network.eth_handle.get()));
      if (!network.eth_glue)
        co_return {olifilo::unexpect, ESP_FAIL, olifilo::esp::error_category()};
      if (const auto status = ::esp_netif_attach(network.netif.get(), network.eth_glue.get()); status != ESP_OK)
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }
    else
#endif
    {
      network.netif.reset(esp_netif_create_default_wifi_sta());
      if (!network.netif)
        co_return {olifilo::unexpect, ESP_FAIL, olifilo::esp::error_category()};

      {
        const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        if (const auto status = esp_wifi_init(&config); status != ESP_OK)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }

      if (const auto status = esp_wifi_set_mode(WIFI_MODE_STA); status != ESP_OK)
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

      {
        wifi_config_t config = {
          .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,     // Scan all channels instead of stopping after first match
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL, // Sort by signal strength and keep up to 4 best APs
            .threshold = { .authmode = WIFI_AUTH_WPA2_PSK, },
          },
        };

        esp_err_t status;
        auto nvs_fs = ::nvs::open_nvs_handle("olifilo", ::NVS_READONLY, &status);
        if (!nvs_fs)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

        if (status = nvs_fs->get_string("sta.ssid", reinterpret_cast<char*>(&config.sta.ssid), sizeof(config.sta.ssid));
            status != ESP_OK)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

        if (status = nvs_fs->get_string("sta.pswd", reinterpret_cast<char*>(&config.sta.password), sizeof(config.sta.password));
            status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
        else if (status == ESP_ERR_NVS_NOT_FOUND)
          config.sta.password[0] = '\0';

        if (const auto status = esp_wifi_set_config(WIFI_IF_STA, &config); status != ESP_OK)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }
    }

    // unsorted, mixed & duplicated on purpose to test (compile-time) sorting & de-duplication
    auto wifi_event_reader = olifilo::esp::events::subscribe<
        IP_EVENT_STA_GOT_IP
      , WIFI_EVENT_STA_DISCONNECTED
      , IP_EVENT_PPP_GOT_IP
      , WIFI_EVENT_STA_CONNECTED
      , IP_EVENT_ETH_GOT_IP
      , IP_EVENT_STA_GOT_IP
#if CONFIG_LWIP_IPV6
      , IP_EVENT_GOT_IP6
#endif
      , ETHERNET_EVENT_CONNECTED
      >();
    if (!wifi_event_reader)
      co_return {olifilo::unexpect, wifi_event_reader.error()};

#if CONFIG_ETH_USE_OPENETH
    if (network.eth_handle)
    {
      if (const auto status = ::esp_eth_start(network.eth_handle.get()); status != ESP_OK)
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }
    else
#endif
    {
      if (const auto status = co_await olifilo::esp::wifi_start(); !status)
        co_return {olifilo::unexpect, status.error()};
    }

#if CONFIG_ETH_USE_OPENETH
    if (!network.eth_handle)
#endif
    {
      if (const auto status = ::esp_wifi_connect(); status != ESP_OK)
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }

    while (true)
    {
      auto event = co_await wifi_event_reader->receive();
      if (!event)
        co_return {olifilo::unexpect, event.error()};
      const auto& [event_id, event_data] = *event;

      ESP_LOGI(TAG, "%d: received event %s:%ld"
          , __LINE__
          , visit([](olifilo::esp::detail::EventIdEnum auto id) { return olifilo::esp::detail::event_id<decltype(id)>::base; }, event_id)
          , visit([](olifilo::esp::detail::EventIdEnum auto id) { return static_cast<std::int32_t>(id); }, event_id)
          );
      if (auto error = visit(
            overloaded{
              [] (const ::ip_event_got_ip_t& ip_event) noexcept -> std::error_code
              {
                const char* const desc = esp_netif_get_desc(ip_event.esp_netif);
                ESP_LOGI(TAG, "Received IPv4 address on interface \"%s\" " IPSTR "/%d gw:" IPSTR ""
                    , desc
                    , IP2STR(&ip_event.ip_info.ip)
                    , std::countl_one(ntohl(ip_event.ip_info.netmask.addr))
                    , IP2STR(&ip_event.ip_info.gw)
                    );
                return {};
              },
#if CONFIG_LWIP_IPV6
              [] (const ::ip_event_got_ip6_t& event) noexcept -> std::error_code
	      {
                const char* const desc = esp_netif_get_desc(event.esp_netif);
                const auto ipv6_type = esp_netif_ip6_get_addr_type(const_cast<::esp_ip6_addr_t*>(&event.ip6_info.ip));
                ESP_LOGI(TAG, "Received IPv6 address on interface \"%s\" " IPV6STR " (type=%d)"
                    , desc
                    , IPV62STR(event.ip6_info.ip)
                    , ipv6_type
                    );

	      	return {};
	      },
#endif
              [netif = network.netif.get()] (const ::esp_eth_handle_t& event) noexcept -> std::error_code
              {
                ESP_LOGI(TAG, "Connected to ETH");
#if CONFIG_LWIP_IPV6
		// Assign link-local IPv6 address (fe80::) to allow SLAAC to start
                if (const auto error = esp_netif_create_ip6_linklocal(netif); error != ESP_OK)
                  return {error, olifilo::esp::error_category()};
#endif
                return {};
              },
              [netif = network.netif.get()] (const ::wifi_event_sta_connected_t& wifi_event) noexcept -> std::error_code
              {
                ESP_LOGI(TAG, "Connected to: %-.*s, BSSID: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx on channel %hhu"
                    , wifi_event.ssid_len, wifi_event.ssid
                    , wifi_event.bssid[0], wifi_event.bssid[1], wifi_event.bssid[2], wifi_event.bssid[3], wifi_event.bssid[4], wifi_event.bssid[5]
                    , wifi_event.channel
                    );
#if CONFIG_LWIP_IPV6
		// Assign link-local IPv6 address (fe80::) to allow SLAAC to start
                if (const auto error = esp_netif_create_ip6_linklocal(netif); error != ESP_OK)
                  return {error, olifilo::esp::error_category()};
#endif
                return {};
              },
              [] (const ::wifi_event_sta_disconnected_t& wifi_event) noexcept -> std::error_code
              {
                ESP_LOGI(TAG, "Disconnected from: %-.*s, BSSID: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx because %hhu"
                    , wifi_event.ssid_len, wifi_event.ssid
                    , wifi_event.bssid[0], wifi_event.bssid[1], wifi_event.bssid[2], wifi_event.bssid[3], wifi_event.bssid[4], wifi_event.bssid[5]
                    , wifi_event.reason
                    );
                if (wifi_event.reason != WIFI_REASON_ROAMING)
                  return {::esp_wifi_connect(), olifilo::esp::error_category()};
                return {};
              },
            }
          , event_data);
          error)
        co_return {olifilo::unexpect, error};
      if (std::holds_alternative<::ip_event_got_ip_t>(event_data))
        co_return network;
#if CONFIG_LWIP_IPV6
      else if (auto* const event = std::get_if<::ip_event_got_ip6_t>(&event_data))
      {
        switch (esp_netif_ip6_get_addr_type(const_cast<::esp_ip6_addr_t*>(&event->ip6_info.ip)))
        {
          case ESP_IP6_ADDR_IS_UNKNOWN:
          case ESP_IP6_ADDR_IS_LINK_LOCAL:
          case ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6:
            break;

          case ESP_IP6_ADDR_IS_GLOBAL:
          case ESP_IP6_ADDR_IS_SITE_LOCAL:
          case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:
            // got a routable address!
            co_return network;
        }
      }
#endif
    }
  }
};
}  // namespace esp

extern int mqtt_main();

extern "C" void app_main()
{
  // Initialize NVS partition
  if (const std::error_code nvs_state{nvs_flash_init(), olifilo::esp::error_category()}; nvs_state
   && (nvs_state != std::errc::io_error))
  {
    ESP_LOGE(TAG, "nvs_flash_init: %s", nvs_state.message().c_str());
    std::abort();
  }
  else if (nvs_state)
  {
    // NVS partition was truncated and needs to be erased
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  auto network = esp::networking::create().get();
  if (!network)
  {
    ESP_LOGE(TAG, "esp::networking::create: %s", network.error().message().c_str());
    std::abort();
  }

  mqtt_main();
}
