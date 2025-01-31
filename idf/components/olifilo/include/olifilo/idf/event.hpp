// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <olifilo/detail/small_vector.hpp>
#include <olifilo/expected.hpp>
#include <olifilo/coro/future.hpp>
#include <olifilo/coro/io/file_descriptor.hpp>

#include "events/base.hpp"
#include "events/detail.hpp"

#include <esp_event_base.h>

namespace olifilo::esp
{
class event_subscription_default
{
  private:
    ::esp_event_base_t _event_base = nullptr;
    std::int32_t _event_id = -1;
    ::esp_event_handler_instance_t _subscription = nullptr;

    constexpr event_subscription_default(
        ::esp_event_base_t event_base
      , std::int32_t event_id
      , ::esp_event_handler_instance_t subscription
      ) noexcept
      : _event_base{event_base}
      , _event_id{event_id}
      , _subscription{subscription}
    {
    }

    expected<void> destroy() noexcept;

  public:
    event_subscription_default() = default;

    static expected<event_subscription_default> create(
        ::esp_event_base_t event_base
      , std::int32_t event_id
      , ::esp_event_handler_t event_handler
      , void* event_handler_arg
      ) noexcept;

    constexpr event_subscription_default(event_subscription_default&& rhs) noexcept
      : _event_base{rhs._event_base}
      , _event_id{rhs._event_id}
      , _subscription{std::exchange(rhs._subscription, nullptr)}

    {
    }

    constexpr event_subscription_default& operator=(event_subscription_default&& rhs) noexcept
    {
      if (*this)
        static_cast<void>(destroy());

      this->_event_base = rhs._event_base;
      this->_event_id = rhs._event_id;
      this->_subscription = std::exchange(rhs._subscription, nullptr);
      return *this;
    }

    constexpr ~event_subscription_default()
    {
      if (*this)
        static_cast<void>(destroy());
    }

    constexpr ::esp_event_base_t base() const noexcept
    {
      return _event_base;
    }

    constexpr std::int32_t id() const noexcept
    {
      return _event_id;
    }

    explicit constexpr operator bool() const noexcept
    {
      return _subscription;
    }
};

class events
{
  public:
    static expected<int> init() noexcept;

    template <detail::EventIdEnum auto... EventIds>
    class subscriber
    {
      public:
        using var_event_id_t = detail::unique_t<std::variant<decltype(EventIds)...>>;
        using var_event_t = std::conditional_t<
            detail::contains_void<detail::event_t<EventIds>...>
          , detail::unique_t<std::variant<
              // ensure std::monostate is the *first* alternative in our variant
              std::monostate
            , std::conditional_t<
                std::is_void_v<detail::event_t<EventIds>>
              , std::monostate
              , detail::event_t<EventIds>
              >...
            >>
          , detail::unique_t<std::variant<detail::event_t<EventIds>...>>
          >;
        static_assert(std::tuple_size_v<detail::unique_t<std::tuple<std::integral_constant<std::size_t, detail::event_id<decltype(EventIds)>::sort_key>...>>>
            == std::variant_size_v<var_event_id_t>, "non-unique sort-key found for used event id enums!");

        using event_id_t = std::conditional_t<
            std::variant_size_v<var_event_id_t> == 1
          , std::variant_alternative_t<0, var_event_id_t>
          , var_event_id_t
          >;
        using event_t = std::conditional_t<
            std::variant_size_v<var_event_t> == 1
          , std::conditional_t<
              detail::contains_void<detail::event_t<EventIds>...>
              && std::is_same_v<std::variant_alternative_t<0, var_event_t>, std::monostate>
            , void
            , std::variant_alternative_t<0, var_event_t>
            >
          , var_event_t
          >;

        static constexpr auto max_event_size = std::max({
            detail::size_of<detail::event_t<EventIds>>...
          });

        using result_t = std::conditional_t<
              sizeof...(EventIds) == 1
            , event_t
            , std::conditional_t<
                std::is_void_v<event_t>
              , event_id_t
              , std::pair<event_id_t, event_t>
              >
            >;

        future<result_t> receive() noexcept
        {
          struct {
            struct {
              ::esp_event_base_t base;
              std::int32_t       id;
            } hdr;
            std::byte data[max_event_size];
          } serialized;
          constexpr auto header_size = sizeof(serialized.hdr);
          auto result = co_await _fd.read_some(as_writable_bytes(std::span(&serialized, 1)));
          if (!result)
            co_return {olifilo::unexpect, result.error()};
          else if (result->size_bytes() < header_size)
            co_return {olifilo::unexpect, make_error_code(std::errc::message_size)};

          co_return detail::decode_event<result_t, event_t, var_event_id_t, 0, EventIds...>(
              serialized.hdr.base
            , serialized.hdr.id
            , result->subspan(header_size)
            );
        }

        explicit constexpr operator bool() const noexcept
        {
          return static_cast<bool>(_fd);
        }

        constexpr auto close() noexcept
        {
          return _fd.close();
        }

      private:
        io::file_descriptor _fd;

        friend class events;
        explicit constexpr subscriber(io::file_descriptor fd) noexcept
          : _fd(std::move(fd))
        {
        }

        static expected<subscriber> create() noexcept
        {
          auto fd = subscribe({
              {detail::event_id<decltype(EventIds)>::base, static_cast<std::int32_t>(EventIds)}...
            }, max_event_size);
          if (!fd)
            return {unexpect, fd.error()};
          return subscriber(*std::move(fd));
        }
    };

    template <detail::EventIdEnum auto... EventIds>
    static constexpr auto subscribe() noexcept
    {
      // use decltype() and call create() ourselves to avoid code-gen for the index_sequence overload
      constexpr auto indices = detail::sort_indices<EventIds...>();
      return std::remove_pointer_t<decltype(subscribe<indices, EventIds...>(std::make_index_sequence<indices.size()>()))>::create();
    }

  private:
    // helper that selects an instance of 'subscriber' with sorted and deduplicated EventIds to reduce instantiations
    template <auto indices, detail::EventIdEnum auto... EventIds, std::size_t... Is>
    static consteval auto subscribe(std::index_sequence<Is...>) noexcept
    {
      using input_t = std::tuple<std::integral_constant<decltype(EventIds), EventIds>...>;
      return static_cast<subscriber<std::tuple_element_t<indices[Is], input_t>::value...>*>(nullptr);
    }

    static expected<io::file_descriptor> subscribe(
        std::initializer_list<std::tuple<::esp_event_base_t, std::int32_t>> events
      , std::size_t event_data_size) noexcept;

  private:
    struct fd_waiter;
    struct fd_context
    {
      std::mutex                              lock;
      std::vector<std::byte>                  queue;
      olifilo::detail::sbo_vector<fd_waiter*> waiters;
      std::vector<event_subscription_default> subscriptions;
      std::size_t                             event_data_size = 0;
      bool                                    opened          = false;

      ~fd_context();
      void receive(this fd_context* self, esp_event_base_t event_base, std::int32_t event_id, void* event_data) noexcept;
    };

    static std::array<fd_context, 5> contexts;
};
}  // namespace olifilo::esp
