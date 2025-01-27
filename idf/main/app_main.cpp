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
#include <soc/soc.h>

#include <olifilo/idf/errors.hpp>
#include <olifilo/idf/event.hpp>

#include "../../src/coro.cpp"

static constexpr char TAG[] = "olifilo-test";

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
    olifilo::esp::event_queue event_queue{std::nothrow};
    if (auto error = event_queue.init(); error)
      co_return {olifilo::unexpect, error};

    if (const auto status = ::esp_netif_init(); status != ESP_OK)
      co_return {olifilo::unexpect, status, olifilo::esp::error_category()};

    networking network;

#if CONFIG_ETH_USE_OPENETH
#if !defined(DR_REG_EMAC_BASE) && (CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3)
#  define DR_REG_EMAC_BASE            0x600CD000
#endif
#ifndef OPENETH_BASE
#  define OPENETH_BASE                DR_REG_EMAC_BASE
#endif
#ifndef OPENETH_MODER_REG
#  define OPENETH_MODER_REG           (OPENETH_BASE + 0x00)
#endif
#ifndef OPENETH_MODER_DEFAULT
#  define OPENETH_MODER_DEFAULT       0xa000
#endif

    ESP_LOGD(TAG, "REG_READ(OPENETH_MODER_REG) (%#lx) == OPENETH_MODER_DEFAULT (%#x)", REG_READ(OPENETH_MODER_REG), OPENETH_MODER_DEFAULT);
    if (REG_READ(OPENETH_MODER_REG) == OPENETH_MODER_DEFAULT)
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
            .ssid = "Tartarus",
            .password = "password123!",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,     // Scan all channels instead of stopping after first match
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL, // Sort by signal strength and keep up to 4 best APs
            .threshold = { .authmode = WIFI_AUTH_WPA2_PSK, },
          },
        };

        if (const auto status = esp_wifi_set_config(WIFI_IF_STA, &config); status != ESP_OK)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }
    }

#if CONFIG_ETH_USE_OPENETH
    if (network.eth_handle)
    {
      if (const auto status = ::esp_eth_start(network.eth_handle.get()); status != ESP_OK)
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }
    else
#endif
    {
      if (const auto status = esp_wifi_start(); status != ESP_OK)
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }

    while (true)
    {
      auto event = co_await event_queue.receive();
      if (!event)
        co_return {olifilo::unexpect, event.error()};
      const auto& [event_id, event_data] = *event;

      if (auto* ip_event = std::get_if<ip_event_got_ip_t>(&event_data))
      {
        const char* const desc = esp_netif_get_desc(ip_event->esp_netif);
        ESP_LOGI(TAG, "Received IPv4 address on interface \"%s\"", desc);
        co_return network;
      }
      else if (event_id == olifilo::esp::event_queue::event_id_t(::WIFI_EVENT_STA_START))
      {
        ESP_LOGI(TAG, "%d", __LINE__);
        if (const auto status = ::esp_wifi_connect(); status != ESP_OK)
          co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }
      else if (auto* wifi_event = std::get_if<wifi_event_sta_connected_t>(&event_data))
      {
        ESP_LOGI(TAG, "Connected to: %-.*s, BSSID: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx on channel %hhu"
            , wifi_event->ssid_len, wifi_event->ssid
            , wifi_event->bssid[0], wifi_event->bssid[1], wifi_event->bssid[2], wifi_event->bssid[3], wifi_event->bssid[4], wifi_event->bssid[5]
            , wifi_event->channel
            );
      }
      else if (auto* wifi_event = std::get_if<wifi_event_sta_disconnected_t>(&event_data))
      {
        ESP_LOGI(TAG, "Disconnected from: %-.*s, BSSID: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx because %hhu"
            , wifi_event->ssid_len, wifi_event->ssid
            , wifi_event->bssid[0], wifi_event->bssid[1], wifi_event->bssid[2], wifi_event->bssid[3], wifi_event->bssid[4], wifi_event->bssid[5]
            , wifi_event->reason
            );
        if (wifi_event->reason != WIFI_REASON_ROAMING)
        {
          if (const auto status = ::esp_wifi_connect(); status != ESP_OK)
            co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
        }
      }
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
