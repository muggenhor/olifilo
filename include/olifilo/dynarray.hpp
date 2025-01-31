// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <type_traits>

#include <olifilo/expected.hpp>
#include <olifilo/utility.hpp>

namespace olifilo
{
template <typename T>
class dynarray
{
  public:
    using value_type = T;

  private:
    static constexpr std::size_t cookie_size = std::max(sizeof(std::size_t), alignof(value_type));
    struct alloc_align_t
    {
      alignas(std::min(sizeof(value_type), cookie_size)) std::byte buf[std::min(sizeof(value_type), cookie_size)];
    };

  public:
    static constexpr expected<dynarray> create(std::size_t count) noexcept
    {
      if (count == 0)
        return dynarray();

      // avoid overflow
      if (count > (std::size_t(-1) - cookie_size) / sizeof(value_type))
        return {unexpect, make_error_code(std::errc::invalid_argument)};

      static_assert(sizeof(value_type) % sizeof(alloc_align_t) == 0);
      static_assert(cookie_size % sizeof(alloc_align_t) == 0);
      const std::size_t size = count * (sizeof(value_type) / sizeof(alloc_align_t)) + cookie_size / sizeof(alloc_align_t);
      auto buf = new (std::nothrow) alloc_align_t[size];
      if (!buf)
        return {unexpect, make_error_code(std::errc::not_enough_memory)};

      // store count *after* data to ensure it doesn't screw with alignment of data
      const auto countp = static_cast<std::size_t*>(static_cast<void*>(buf->buf + cookie_size - sizeof(std::size_t)));
      std::construct_at(countp, count);

      const auto datap = static_cast<value_type*>(static_cast<void*>(buf->buf + cookie_size));
      std::uninitialized_default_construct_n(datap, count);
      return dynarray(datap);
    }

    dynarray() = default;

    explicit constexpr dynarray(void* data) noexcept
      : _data(static_cast<value_type*>(data))
    {
    }

    constexpr dynarray(dynarray&& rhs) noexcept
      : _data(std::exchange(rhs._data, nullptr))
    {
    }

    constexpr dynarray& operator=(dynarray&& rhs) noexcept
    {
      dynarray _(std::move(*this));
      _data = std::exchange(rhs._data, nullptr);
      return *this;
    }

    constexpr ~dynarray()
    {
      if (_data)
      {
        const auto countp = static_cast<const std::size_t*>(static_cast<const void*>(_data)) - 1;
        std::destroy_n(_data, *countp);
        std::destroy_at(countp);

        const auto buf = static_cast<alloc_align_t*>(static_cast<void*>(static_cast<std::byte*>(static_cast<void*>(_data)) - cookie_size));
        delete [] buf;
      }
    }

    constexpr void* release() noexcept
    {
      return std::exchange(_data, nullptr);
    }

    constexpr bool empty() const noexcept
    {
      return !_data;
    }

    constexpr std::size_t size() const noexcept
    {
      if (!_data)
        return 0;
      return *(static_cast<const std::size_t*>(static_cast<const void*>(_data)) - 1);
    }

    template <typename Self>
    constexpr auto begin(this Self&& self) noexcept -> cv_like_t<Self, value_type*>
    {
      return self._data;
    }

    template <typename Self>
    constexpr auto end(this Self&& self) noexcept -> cv_like_t<Self, value_type*>
    {
      return self._data + self.size();
    }

  private:
    explicit constexpr dynarray(value_type* count) noexcept
      : _data(count)
    {
    }

    value_type* _data = nullptr;
};
}  // namespace olifilo
