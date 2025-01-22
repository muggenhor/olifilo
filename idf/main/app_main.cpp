#include <esp_err.h>
#include <esp_eth.h>
#include <esp_eth_mac.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_vfs_eventfd.h>

#include "../../src/coro.cpp"

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
}  // namespace esp

static constexpr char TAG[] = "olifilo-test";
static esp_eth_handle_t s_eth_handle = nullptr;
static esp_eth_mac_t* s_mac = nullptr;
static esp_eth_phy_t* s_phy = nullptr;
static esp_eth_netif_glue_handle_t s_eth_glue = nullptr;

struct eventfd_notify
{
  olifilo::io::file_descriptor fd;
};

static void eth_on_got_ip(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data)
{
  auto&& event = *static_cast<const ip_event_got_ip_t*>(event_data);
  const char* const desc = esp_netif_get_desc(event.esp_netif);
  if (strncmp(desc, "openeth", 7) != 0)
    return;
  ESP_LOGI(TAG, "Received IPv4 address on interface \"%s\"", desc);
  auto&& notifier = *static_cast<eventfd_notify*>(arg);
  std::uint64_t eventnum = 1;
  notifier.fd.write(as_bytes(std::span(&eventnum, 1))).get().value();
}

static olifilo::future<void> init_networking()
{
  if (const auto status = ::esp_event_loop_create_default(); status != ESP_OK)
    co_return {olifilo::unexpect, status, esp::error_category()};
  if (const auto status = ::esp_netif_init(); status != ESP_OK)
    co_return {olifilo::unexpect, status, esp::error_category()};

  constexpr auto eventfd_config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
  if (const auto status = ::esp_vfs_eventfd_register(&eventfd_config); status != ESP_OK)
    co_return {olifilo::unexpect, status, esp::error_category()};

  esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
  // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
  esp_netif_config.if_desc = "openeth";
  esp_netif_config.route_prio = 64;
  esp_netif_config_t netif_config = {
      .base = &esp_netif_config,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
  };
  esp_netif_t* const netif = esp_netif_new(&netif_config);
  if (!netif)
    co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  mac_config.rx_task_stack_size = 2048;
  s_mac = esp_eth_mac_new_openeth(&mac_config);
  if (!s_mac)
    co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = 5;
  phy_config.autonego_timeout_ms = 100;
  s_phy = esp_eth_phy_new_dp83848(&phy_config);
  if (!s_phy)
    co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};

  // Install Ethernet driver
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
  if (const auto status = ::esp_eth_driver_install(&config, &s_eth_handle); status != ESP_OK)
    co_return {olifilo::unexpect, status, esp::error_category()};

  // combine driver with netif
  s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
  if (!s_eth_glue)
    co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
  if (const auto status = ::esp_netif_attach(netif, s_eth_glue); status != ESP_OK)
    co_return {olifilo::unexpect, status, esp::error_category()};

  // Register user defined event handers
  eventfd_notify notifier{olifilo::io::file_descriptor_handle(eventfd(0, 0))};
  if (!notifier.fd)
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

  if (const auto status = ::esp_eth_start(s_eth_handle); status != ESP_OK)
    co_return {olifilo::unexpect, status, esp::error_category()};

  std::uint64_t event;
  if (const auto r = co_await notifier.fd.read(as_writable_bytes(std::span(&event, 1)), olifilo::eagerness::lazy); !r)
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

  co_return {};
}

extern int mqtt_main();

extern "C" void app_main()
{
  if (auto error = init_networking().get().error(); error)
  {
    ESP_LOGE(TAG, "init_networking: %s", error.message().c_str());
    std::abort();
  }

  mqtt_main();
}
