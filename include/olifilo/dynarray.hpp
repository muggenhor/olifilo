// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <type_traits>

#include <olifilo/expected.hpp>

namespace olifilo
{
template <typename T>
class dynarray
{
  public:
    using value_type = T;

    static constexpr expected<dynarray> create(std::size_t count) noexcept
    {
      if (count == 0)
        return dynarray();

      // avoid overflow
      if (count > (std::size_t(-1) - sizeof(std::size_t)) / sizeof(value_type))
        return {unexpect, make_error_code(std::errc::invalid_argument)};

      const std::size_t size = count * sizeof(value_type) + sizeof(std::size_t);
      auto buf = new (std::nothrow) std::byte[size];
      if (!buf)
        return {unexpect, make_error_code(std::errc::not_enough_memory)};

      // store count *after* data to ensure it doesn't screw with alignment of data
      const auto countp = static_cast<std::size_t*>(static_cast<void*>(buf + count * sizeof(value_type)));
      static_assert(alignof(value_type) % alignof(*countp) == 0);
      std::construct_at(countp, count);

      std::ranges::uninitialized_default_construct(std::span(static_cast<value_type*>(static_cast<void*>(buf)), count));
      return dynarray(countp);
    }

    constexpr dynarray() noexcept
      : _count(nullptr)
    {
    }

    explicit constexpr dynarray(void* count) noexcept
      : _count(static_cast<std::size_t*>(count))
    {
    }

    constexpr dynarray(dynarray&& rhs) noexcept
      : _count(std::exchange(rhs._count, nullptr))
    {
    }

    constexpr dynarray& operator=(dynarray&& rhs) noexcept
    {
      dynarray _(std::move(*this));
      _count = std::exchange(rhs._count, nullptr);
      return *this;
    }

    constexpr ~dynarray()
    {
      if (_count)
      {
        std::destroy(begin(), end());
        auto buf = static_cast<std::byte*>(static_cast<void*>(_count)) - *_count * sizeof(value_type);
        std::destroy_at(_count);
        delete [] buf;
      }
    }

    constexpr void* release() noexcept
    {
      return std::exchange(_count, nullptr);
    }

    template <typename Self>
    constexpr auto end(this Self&& self) noexcept -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const value_type*, value_type*>
    {
      return static_cast<value_type*>(static_cast<void*>(self._count));
    }

    template <typename Self>
    constexpr auto begin(this Self&& self) noexcept -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const value_type*, value_type*>
    {
      if (!self._count)
        return nullptr;
      return std::forward<Self>(self).end() - *self._count;
    }

  private:
    explicit constexpr dynarray(std::size_t* count) noexcept
      : _count(count)
    {
    }

    std::size_t* _count;
};
}  // namespace olifilo
