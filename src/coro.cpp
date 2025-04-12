// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/coro/future.hpp>
#include <olifilo/coro/when_all.hpp>
#include <olifilo/coro/when_any.hpp>
#include <olifilo/expected.hpp>
#include <olifilo/io/poll.hpp>
#include <olifilo/io/types.hpp>
#include <olifilo/mqtt.hpp>

#include "logging-stuff.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

#include <arpa/inet.h>
#include <inttypes.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#include <nvs_handle.hpp>
#else
#include <cstdlib>
#include <cstring>
#endif

namespace olifilo
{
future<void> sleep_until(io::poll::timeout_clock::time_point time) noexcept
{
  ////auto timeout = time - io::poll::timeout_clock::now();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}@{})\n", ts(), __LINE__, "sleep_until", std::chrono::duration_cast<std::chrono::milliseconds>(timeout), std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()));

  if (auto r = co_await io::poll(time);
      !r && r.error() != std::errc::timed_out)
    co_return r;
  else
    co_return {};
}

future<void> sleep(io::poll::timeout_clock::duration time) noexcept
{
  return sleep_until(time + io::poll::timeout_clock::now());
}
}  // namespace olifilo

olifilo::future<void> do_mqtt(std::uint8_t id) noexcept
{
  using namespace std::literals::chrono_literals;

  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({})\n", ts(), __LINE__, "do_mqtt", id);

  static constexpr char mqtt_default_host[] = "fdce:1234:5678::1";
  std::string_view host = mqtt_default_host;
  std::uint16_t port = 1883;
  std::optional<std::string_view> username;
  std::optional<std::string_view> password;
  std::string mqtt_con_data; // buffer to hold the above views in a single allocation
#ifdef ESP_PLATFORM
  if (auto nvs_fs = []() -> olifilo::expected<std::unique_ptr<::nvs::NVSHandle>> {
      esp_err_t err;
      if (auto nvs_fs = ::nvs::open_nvs_handle("olifilo", ::NVS_READONLY, &err); nvs_fs)
        return nvs_fs;
      return {olifilo::unexpect, err, olifilo::esp::error_category()};
    }(); !nvs_fs && nvs_fs.error().value() != ESP_ERR_NVS_NOT_FOUND)
  {
    co_return {olifilo::unexpect, nvs_fs.error()};
  }
  else if (nvs_fs)
  {
    if (const auto status = (*nvs_fs)->get_item("mqtt.port", port);
        status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND)
    {
      ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining 'mqtt.port': %s"
          , std::error_code(status, olifilo::esp::error_category()).message().c_str());
      co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
    }

    ESP_LOGD("olifilo-coro", "do_mqtt: mqtt.port=%" PRIu16, port);

    olifilo::expected<std::size_t> host_size(std::in_place);
    if (const auto status = (*nvs_fs)->get_item_size(::nvs::ItemType::SZ, "mqtt.host", *host_size);
        status != ESP_OK)
      host_size = {olifilo::unexpect, status, olifilo::esp::error_category()};
    if (!host_size && host_size.error() != std::errc::no_such_file_or_directory)
    {
      ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining size of 'mqtt.host': %s"
          , host_size.error().message().c_str());
      co_return host_size.error();
    }

    olifilo::expected<std::size_t> user_size(std::in_place);
    if (const auto status = (*nvs_fs)->get_item_size(::nvs::ItemType::SZ, "mqtt.username", *user_size);
        status != ESP_OK)
      user_size = {olifilo::unexpect, status, olifilo::esp::error_category()};
    if (!user_size && user_size.error() != std::errc::no_such_file_or_directory)
    {
      ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining size of 'mqtt.username': %s"
          , user_size.error().message().c_str());
      co_return user_size.error();
    }

    olifilo::expected<std::size_t> pass_size(std::in_place);
    if (const auto status = (*nvs_fs)->get_item_size(::nvs::ItemType::SZ, "mqtt.password", *pass_size);
        status != ESP_OK)
      pass_size = {olifilo::unexpect, status, olifilo::esp::error_category()};
    if (!pass_size && pass_size.error() != std::errc::no_such_file_or_directory)
    {
      ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining size of 'mqtt.password': %s"
          , pass_size.error().message().c_str());
      co_return pass_size.error();
    }

    mqtt_con_data.resize(0
      + (user_size ? *user_size - 1 : 0)
      + (pass_size ? *pass_size - 1 : 0)
      + (host_size ? *host_size - 1 : 0)
      );

    char* storage_start = &mqtt_con_data[0];
    if (user_size)
    {
      ESP_LOGD("olifilo-coro", "do_mqtt: sizeof(mqtt.username)=%zu", *user_size - 1);
      if (const auto status = (*nvs_fs)->get_string("mqtt.username", storage_start, *user_size);
          status != ESP_OK)
      {
        ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining 'mqtt.username': %s"
            , esp_err_to_name(status));
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }
      username = std::string_view(storage_start, *user_size - 1);
      storage_start += username->size();

      ESP_LOGD("olifilo-coro", "do_mqtt: mqtt.username=\"%.*s\"", username->size(), username->data());
    }
    if (pass_size)
    {
      ESP_LOGD("olifilo-coro", "do_mqtt: sizeof(mqtt.password)=%zu", *pass_size - 1);
      if (const auto status = (*nvs_fs)->get_string("mqtt.password", storage_start, *pass_size);
          status != ESP_OK)
      {
        ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining 'mqtt.password': %s"
            , esp_err_to_name(status));
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }
      password = std::string_view(storage_start, *pass_size - 1);
      storage_start += password->size();

      ESP_LOGD("olifilo-coro", "do_mqtt: mqtt.password=\"%.*s\"", password->size(), password->data());
    }
    if (host_size) // NOTE: 'host' must be the last item in 'mqtt_con_data' to ensure it's NUL terminated
    {
      ESP_LOGD("olifilo-coro", "do_mqtt: sizeof(mqtt.host)=%zu", *host_size - 1);
      if (const auto status = (*nvs_fs)->get_string("mqtt.host", storage_start, *host_size);
          status != ESP_OK)
      {
        ESP_LOGE("olifilo-coro", "do_mqtt: error while obtaining 'mqtt.host': %s"
            , esp_err_to_name(status));
        co_return {olifilo::unexpect, status, olifilo::esp::error_category()};
      }
      host = std::string_view(storage_start, *host_size - 1);
      storage_start += *host_size;

      ESP_LOGD("olifilo-coro", "do_mqtt: mqtt.host=\"%.*s\"", host.size(), host.data());
    }
  }
#else
  if (const char* const e_host = std::getenv("MQTT_HOST"); e_host && e_host[0])
    host = e_host;
  if (const char* const e_port = std::getenv("MQTT_PORT"); e_port && e_port[0])
  {
    const auto portlen = std::strlen(e_port);
    if (auto status = std::from_chars(e_port, e_port + portlen, port);
        status.ec != std::errc())
      co_return {olifilo::unexpect, std::make_error_code(status.ec)};
    else if (status.ptr != e_port + portlen
          || port <= 0)
      co_return {olifilo::unexpect, std::make_error_code(std::errc::invalid_argument)};
  }
  if (const char* const user = std::getenv("MQTT_USERNAME"); user && user[0])
  {
    username = user;
    if (const char* const pass = std::getenv("MQTT_PASSWORD"); pass)
      password = pass;
  }
#endif

  auto r = co_await olifilo::io::mqtt::connect(host.data(), port, id, username, password);
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) r = {}\n", ts(), __LINE__, "do_mqtt", id, static_cast<bool>(r));
  if (!r)
    co_return r;

  using clock = olifilo::io::poll::timeout_clock;
  const auto keep_alive_wait_time = std::chrono::duration_cast<clock::duration>(r->keep_alive) * 3 / 4;
  const auto start = clock::now() - ts();
  constexpr auto run_time = 120s;

  clock::time_point now;
  while ((now = clock::now()) - start < run_time)
  {
    const auto sleep_time = keep_alive_wait_time - (now - start) % keep_alive_wait_time;
    auto err = co_await olifilo::sleep_until(now + sleep_time);
    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
    if (!err)
      co_return err;

    if (clock::now() - start >= run_time)
      break;

    err = co_await r->ping();
    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
    if (!err)
      co_return err;
  }

  auto err = co_await r->disconnect();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
  co_return err;
}

#ifdef ESP_PLATFORM
int mqtt_main()
#else
int main()
#endif
{
  using namespace std::literals::chrono_literals;
  using olifilo::when_all;
  using olifilo::when_any;

#if 0
  if (auto r = when_any(std::array{
        do_mqtt(0),
      }, 30s).get();
      !r)
  {
#if __cpp_exceptions
    throw std::system_error(r.error());
#else
    ESP_LOGE("olifilo-coro", "mqtt_main: error: %s", r.error().message().c_str());
    std::abort();
#endif
  }
  else if (auto ri = r->futures[r->index].get();
      !ri)
  {
#if __cpp_exceptions
    throw std::system_error(ri.error());
#else
    ESP_LOGE("olifilo-coro", "mqtt_main: error(%u): %s", r->index, ri.error().message().c_str());
    std::abort();
#endif
  }
#else
  if (auto [r1, r2, rs] = std::apply(when_all, std::tuple{
        do_mqtt(1)
      , []() -> olifilo::future<void> {
          auto ra = co_await when_any(
              do_mqtt(2)
            , 30s
            );
          if (ra.error() == std::errc::timed_out)
            co_return {};
          else if (!ra)
            co_return {olifilo::unexpect, ra.error()};
          assert(ra->index == 0);
          co_return co_await std::get<0>(ra->futures);
        }()
      , []() -> olifilo::future<std::vector<olifilo::expected<void>>> {
          auto ra = co_await when_any(std::array{
              []() -> olifilo::future<void> {
                auto ra = co_await when_all(std::array{
                  do_mqtt(3),
                  do_mqtt(4),
                });
                if (!ra)
                  co_return {olifilo::unexpect, ra.error()};
                for (auto& ri : *ra)
                  if (!ri)
                    co_return {olifilo::unexpect, ri.error()};

                co_return {};
              }(),
            }, 45s);
          if (ra.error() == std::errc::timed_out)
            co_return {std::in_place};
          else if (!ra)
            co_return {olifilo::unexpect, ra.error()};
          std::vector<olifilo::expected<void>> rv;
          for (auto& fut: ra->futures)
            if (fut.done())
              rv.emplace_back(co_await fut);
          co_return rv;
        }()
      }).get().value();
      !r1 || !r2 || !rs)
  {
    auto err = !r1 ? r1.error() : !r2 ? r2.error() : rs.error();
#if __cpp_exceptions
    throw std::system_error(err);
#else
    ESP_LOGE("olifilo-coro", "mqtt_main: error: %s", err.message().c_str());
    std::abort();
#endif
  }
  else
  {
    for (auto& ri : *rs)
    {
      if (!ri)
      {
#if __cpp_exceptions
        throw std::system_error(ri.error());
#else
        ESP_LOGE("olifilo-coro", "mqtt_main: error ri: %s", ri.error().message().c_str());
        std::abort();
#endif
      }
    }
  }
#endif
  return 0;
}
