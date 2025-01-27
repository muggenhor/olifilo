#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <esp_err.h>
#include <esp_eth.h>
#include <esp_eth_mac.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_vfs_eventfd.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <soc/soc.h>

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

  bool equivalent(int code, const std::error_condition& condition) const noexcept override
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

class event_subscription_default
{
  private:
    ::esp_event_base_t event_base = nullptr;
    std::int32_t event_id = -1;
    ::esp_event_handler_instance_t subscription = nullptr;

    constexpr event_subscription_default(
        ::esp_event_base_t event_base
      , std::int32_t event_id
      , ::esp_event_handler_instance_t subscription
      ) noexcept
      : event_base{event_base}
      , event_id{event_id}
      , subscription{subscription}
    {
    }

  public:
    event_subscription_default() = default;

    static olifilo::expected<event_subscription_default> create(
        ::esp_event_base_t event_base
      , std::int32_t event_id
      , ::esp_event_handler_t event_handler
      , void* event_handler_arg
      ) noexcept
    {
      ::esp_event_handler_instance_t subscription;
      if (const auto status = ::esp_event_handler_instance_register(event_base, event_id, event_handler, event_handler_arg, &subscription); status != ESP_OK)
        return {olifilo::unexpect, status, esp::error_category()};
      return event_subscription_default(event_base, event_id, subscription);
    }

    constexpr event_subscription_default(event_subscription_default&& rhs) noexcept
      : event_base{rhs.event_base}
      , event_id{rhs.event_id}
      , subscription{std::exchange(rhs.subscription, nullptr)}
    {
    }

    event_subscription_default& operator=(event_subscription_default&& rhs) noexcept
    {
      if (subscription)
        esp_event_handler_instance_unregister(event_base, event_id, std::exchange(subscription, nullptr));

      this->event_base = rhs.event_base;
      this->event_id = rhs.event_id;
      this->subscription = std::exchange(rhs.subscription, nullptr);
      return *this;
    }

    ~event_subscription_default()
    {
      if (subscription)
        esp_event_handler_instance_unregister(event_base, event_id, subscription);
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

  struct wifi_event_sta_start_t {};
  struct eth_event_connected_t { void* driver; };
  struct eth_event_disconnected_t { void* driver; };
  struct event_queue_t
  {
    using event_t = std::variant<
        ip_event_got_ip_t
      , eth_event_connected_t
      , eth_event_disconnected_t
      , wifi_event_sta_start_t
      , wifi_event_sta_connected_t
      , wifi_event_sta_disconnected_t
      >;
    std::vector<event_t> events;
    std::mutex event_lock;
    olifilo::io::file_descriptor notifier;
    event_subscription_default subscription;

    static void receive(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data) noexcept
    {
      auto&& self = *static_cast<event_queue_t*>(arg);
      std::scoped_lock _(self.event_lock);
      if (event_base == IP_EVENT)
      {
        switch (static_cast<ip_event_t>(event_id))
        {
          case IP_EVENT_STA_GOT_IP:
          case IP_EVENT_ETH_GOT_IP:
          case IP_EVENT_PPP_GOT_IP:
            self.events.emplace_back(std::in_place_type<ip_event_got_ip_t>, *static_cast<const ip_event_got_ip_t*>(event_data));
            break;

          default:
            return; // ignore
        }
      }
      else if (event_base == ETH_EVENT)
      {
        switch (static_cast<eth_event_t>(event_id))
        {
          case ETHERNET_EVENT_CONNECTED:
            self.events.emplace_back(std::in_place_type<eth_event_connected_t>, *static_cast<void* const*>(event_data));
            break;
          case ETHERNET_EVENT_DISCONNECTED:
            self.events.emplace_back(std::in_place_type<eth_event_disconnected_t>, *static_cast<void* const*>(event_data));
            break;

          default:
            return; // ignore
        }
      }
      else if (event_base == WIFI_EVENT)
      {
        switch (static_cast<wifi_event_t>(event_id))
        {
          case WIFI_EVENT_STA_START:
            self.events.emplace_back(std::in_place_type<wifi_event_sta_start_t>);
            break;
          case WIFI_EVENT_STA_CONNECTED:
            self.events.emplace_back(std::in_place_type<wifi_event_sta_connected_t>, *static_cast<const wifi_event_sta_connected_t*>(event_data));
            break;
          case WIFI_EVENT_STA_DISCONNECTED:
            self.events.emplace_back(std::in_place_type<wifi_event_sta_disconnected_t>, *static_cast<const wifi_event_sta_disconnected_t*>(event_data));
            break;

          default:
            return; // ignore
        }
      }
      else
      {
        return;
      }

      const std::uint64_t eventnum = 1;
      self.notifier.write(as_bytes(std::span(&eventnum, 1))).get().value();
    }

    olifilo::future<event_t> receive() noexcept
    {
      assert(notifier && "used nothrow constructor without calling init()!");

      while (true)
      {
        {
          std::scoped_lock _(event_lock);
          if (!events.empty())
          {
            auto rv = events.front();
            events.erase(events.begin());
            co_return rv;
          }
        }

        std::uint64_t event_count;
        if (const auto r = co_await notifier.read(as_writable_bytes(std::span(&event_count, 1)), olifilo::eagerness::lazy); !r)
          co_return {olifilo::unexpect, r.error()};
        else if (r->size_bytes() != sizeof(event_count))
        {
          ESP_LOGE(TAG, "event_count size: %u", r->size_bytes());
          co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
        }
        else if (event_count <= 0)
        {
          ESP_LOGE(TAG, "event_count: %llu", event_count);
          co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};
        }
        ESP_LOGD(TAG, "%s: received %lu events", __PRETTY_FUNCTION__, static_cast<std::uint32_t>(event_count));
      }
    }

    std::error_code init() noexcept
    {
      if (notifier)
        return {};

      notifier = olifilo::io::file_descriptor_handle(eventfd(0, 0));
      if (!notifier)
        return {errno, std::system_category()};
      if (auto subscription = event_subscription_default::create(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, (esp_event_handler_t)&receive, this); !subscription)
      {
        notifier = nullptr;
        return subscription.error();
      }
      else
      {
        this->subscription = std::move(*subscription);
      }
      return {};
    }

    event_queue_t()
    {
      if (auto error = init())
#if __cpp_exceptions
        throw std::system_error(error);
#else
      	std::abort();
#endif
    }

    constexpr event_queue_t(std::nothrow_t) noexcept
    {
    }
  };

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
    }
    else
#endif
    {
      network.netif.reset(esp_netif_create_default_wifi_sta());
      if (!network.netif)
        co_return {olifilo::unexpect, ESP_FAIL, esp::error_category()};

      {
        const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        if (const auto status = esp_wifi_init(&config); status != ESP_OK)
          co_return {olifilo::unexpect, status, esp::error_category()};
      }

      if (const auto status = esp_wifi_set_mode(WIFI_MODE_STA); status != ESP_OK)
        co_return {olifilo::unexpect, status, esp::error_category()};

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
          co_return {olifilo::unexpect, status, esp::error_category()};
      }
    }

    event_queue_t event_queue{std::nothrow};
    if (auto error = event_queue.init(); error)
      co_return {olifilo::unexpect, error};

#if CONFIG_ETH_USE_OPENETH
    if (network.eth_handle)
    {
      if (const auto status = ::esp_eth_start(network.eth_handle.get()); status != ESP_OK)
        co_return {olifilo::unexpect, status, esp::error_category()};
    }
    else
#endif
    {
      if (const auto status = esp_wifi_start(); status != ESP_OK)
        co_return {olifilo::unexpect, status, esp::error_category()};
    }

    while (true)
    {
      auto event = co_await event_queue.receive();
      if (!event)
        co_return {olifilo::unexpect, event.error()};

      if (auto* ip_event = std::get_if<ip_event_got_ip_t>(&*event))
      {
        const char* const desc = esp_netif_get_desc(ip_event->esp_netif);
        ESP_LOGI(TAG, "Received IPv4 address on interface \"%s\"", desc);
        co_return network;
      }
      else if (std::holds_alternative<wifi_event_sta_start_t>(*event))
      {
        ESP_LOGI(TAG, "%d", __LINE__);
        if (const auto status = ::esp_wifi_connect(); status != ESP_OK)
          co_return {olifilo::unexpect, status, esp::error_category()};
      }
      else if (auto* wifi_event = std::get_if<wifi_event_sta_connected_t>(&*event))
      {
        ESP_LOGI(TAG, "Connected to: %-.*s, BSSID: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx on channel %hhu"
            , wifi_event->ssid_len, wifi_event->ssid
            , wifi_event->bssid[0], wifi_event->bssid[1], wifi_event->bssid[2], wifi_event->bssid[3], wifi_event->bssid[4], wifi_event->bssid[5]
            , wifi_event->channel
            );
      }
      else if (auto* wifi_event = std::get_if<wifi_event_sta_disconnected_t>(&*event))
      {
        ESP_LOGI(TAG, "Disconnected from: %-.*s, BSSID: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx because %hhu"
            , wifi_event->ssid_len, wifi_event->ssid
            , wifi_event->bssid[0], wifi_event->bssid[1], wifi_event->bssid[2], wifi_event->bssid[3], wifi_event->bssid[4], wifi_event->bssid[5]
            , wifi_event->reason
            );
        if (wifi_event->reason != WIFI_REASON_ROAMING)
        {
          if (const auto status = ::esp_wifi_connect(); status != ESP_OK)
            co_return {olifilo::unexpect, status, esp::error_category()};
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
  if (const std::error_code nvs_state{nvs_flash_init(), esp::error_category()}; nvs_state
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
