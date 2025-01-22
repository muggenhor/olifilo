#include <esp_err.h>
#include <esp_eth.h>
#include <esp_eth_mac.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_vfs_eventfd.h>
#include <memory>
#include <type_traits>

#include "../../src/coro.cpp"

static constexpr char TAG[] = "olifilo-test";

namespace esp
{
struct error_category_t : std::error_category
{
  const char* name() const noexcept override
  {
    return "esp-error";
  }

  std::string message(int ev) const override
  {
    return esp_err_to_name(ev);
  }
};

constexpr const error_category_t& error_category() noexcept
{
  static error_category_t cat;
  return cat;
}

struct eth_deleter
{
  static void operator()(esp_netif_t* netif) noexcept
  {
    ::esp_netif_destroy(netif);
  }

  static olifilo::expected<void> operator()(esp_eth_handle_t handle) noexcept
  {
    if (const auto status = ::esp_eth_stop(handle); status != ESP_OK)
      return {olifilo::unexpect, status, esp::error_category()};
    if (const auto status = ::esp_eth_driver_uninstall(handle); status != ESP_OK)
      return {olifilo::unexpect, status, esp::error_category()};
    return {};
  }

  static olifilo::expected<void> operator()(esp_eth_netif_glue_handle_t glue) noexcept
  {
    if (const auto status = ::esp_eth_del_netif_glue(glue); status != ESP_OK)
      return {olifilo::unexpect, status, esp::error_category()};
    return {};
  }

  static olifilo::expected<void> operator()(esp_eth_mac_t* mac) noexcept
  {
    if (!mac)
      return {olifilo::unexpect, make_error_code(std::errc::invalid_argument)};
    if (const auto status = mac->del(mac); status != ESP_OK)
      return {olifilo::unexpect, status, esp::error_category()};
    return {};
  }

  static olifilo::expected<void> operator()(esp_eth_phy_t* phy) noexcept
  {
    if (!phy)
      return {olifilo::unexpect, make_error_code(std::errc::invalid_argument)};
    if (const auto status = phy->del(phy); status != ESP_OK)
      return {olifilo::unexpect, status, esp::error_category()};
    return {};
  }
};

struct networking
{
  std::unique_ptr<esp_netif_t, esp::eth_deleter> netif;
  std::unique_ptr<esp_eth_mac_t, esp::eth_deleter> mac;
  std::unique_ptr<esp_eth_phy_t, esp::eth_deleter> phy;
  std::unique_ptr<std::remove_pointer_t<esp_eth_handle_t>, esp::eth_deleter> eth_handle;
  std::unique_ptr<std::remove_pointer_t<esp_eth_netif_glue_handle_t>, esp::eth_deleter> eth_glue;

  using eventfd_notify = olifilo::io::file_descriptor;

  static void eth_on_got_ip(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data)
  {
    auto&& event = *static_cast<const ip_event_got_ip_t*>(event_data);
    const char* const desc = esp_netif_get_desc(event.esp_netif);
    if (strncmp(desc, "openeth", 7) != 0)
      return;
    ESP_LOGI(TAG, "Received IPv4 address on interface \"%s\"", desc);
    auto&& notifier = *static_cast<eventfd_notify*>(arg);
    std::uint64_t eventnum = 1;
    notifier.write(as_bytes(std::span(&eventnum, 1))).get().value();
  }

  static olifilo::future<networking> create() noexcept
  {
    if (const auto status = ::esp_event_loop_create_default(); status != ESP_OK && status != ESP_ERR_INVALID_STATE)
      co_return {olifilo::unexpect, status, esp::error_category()};
    if (const auto status = ::esp_netif_init(); status != ESP_OK)
      co_return {olifilo::unexpect, status, esp::error_category()};

    {
      constexpr auto config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
      if (const auto status = ::esp_vfs_eventfd_register(&config); status != ESP_OK && status != ESP_ERR_INVALID_STATE)
        co_return {olifilo::unexpect, status, esp::error_category()};
    }

    networking network;

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
        co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
    }

    {
      eth_mac_config_t config = ETH_MAC_DEFAULT_CONFIG();
      config.rx_task_stack_size = 2048;
      network.mac.reset(esp_eth_mac_new_openeth(&config));
      if (!network.mac)
        co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
    }

    {
      eth_phy_config_t config = ETH_PHY_DEFAULT_CONFIG();
      config.phy_addr = 1;
      config.reset_gpio_num = 5;
      config.autonego_timeout_ms = 100;
      network.phy.reset(esp_eth_phy_new_dp83848(&config));
      if (!network.phy)
        co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
    }

    // Install Ethernet driver
    {
      esp_eth_config_t config = ETH_DEFAULT_CONFIG(network.mac.get(), network.phy.get());
      esp_eth_handle_t handle;
      if (const auto status = ::esp_eth_driver_install(&config, &handle); status != ESP_OK)
        co_return {olifilo::unexpect, status, esp::error_category()};
      network.eth_handle.reset(handle);
    }

    // combine driver with netif
    network.eth_glue.reset(esp_eth_new_netif_glue(network.eth_handle.get()));
    if (!network.eth_glue)
      co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
    if (const auto status = ::esp_netif_attach(network.netif.get(), network.eth_glue.get()); status != ESP_OK)
      co_return {olifilo::unexpect, status, esp::error_category()};

    // Register user defined event handers
    eventfd_notify notifier{olifilo::io::file_descriptor_handle(eventfd(0, 0))};
    if (!notifier)
      co_return {olifilo::unexpect, errno, std::system_category()};
    if (const auto status = ::esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_on_got_ip, &notifier); status != ESP_OK)
      co_return {olifilo::unexpect, status, esp::error_category()};
    struct scope_exit
    {
      ~scope_exit()
      {
        ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_on_got_ip));
      }
    } scope_exit;

    if (const auto status = ::esp_eth_start(network.eth_handle.get()); status != ESP_OK)
      co_return {olifilo::unexpect, status, esp::error_category()};

    std::uint64_t event;
    if (const auto r = co_await notifier.read(as_writable_bytes(std::span(&event, 1)), olifilo::eagerness::lazy); !r)
      co_return {olifilo::unexpect, r.error()};
    else if (r->size_bytes() != sizeof(event))
    {
      ESP_LOGE(TAG, "event size: %u", r->size_bytes());
      co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
    }
    else if (event != 1)
    {
      ESP_LOGE(TAG, "event: %llu", event);
      co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
    }

    co_return network;
  }
};
}  // namespace esp

extern int mqtt_main();

extern "C" void app_main()
{
  auto network = esp::networking::create().get();
  if (!network)
  {
    ESP_LOGE(TAG, "esp::networking::create: %s", network.error().message().c_str());
    std::abort();
  }

  mqtt_main();
}
