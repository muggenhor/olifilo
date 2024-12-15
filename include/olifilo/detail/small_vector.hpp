// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include <olifilo/expected.hpp>

#if defined(__has_feature) && __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif

namespace olifilo::detail
{
// Small-Buffer-Optimized vector specialized for storing pointers
// We only rarely store more than a single pointer in the places that we use this.
// So SBO with 1 would be perfect. But we're storing the default layout used by most std::vector
// implementations here, which is 3 pointers. We use the last as a marker to distinguish between
// the two cases.
//
// We could probably do better if, instead of 'finish' and 'end_of_storage' pointers we store
// 'cur_size' and 'max_capacity' 'size_type' integers where 'size_type' is selected such that
// sizeof(size_type) * 2 <= alignof(T*) && alignof(T*) % alignof(size_type) == 0. On AMD64 with
// std::uint32_t as size_type that would make this structure have SBO==1 exactly. At the "cost" of
// limiting coroutines to "only" being able to wait on 2^32 futures/events. This is probably fine.
// In the worst case we could always build a tree structure (as-if using nested when_all/when_any)
// to circumvent this limit.
template <typename T>
struct sbo_vector
{
  constexpr sbo_vector() noexcept
    : small{}
  {
  }

  // we (currently) don't need copying so don't bother implementing it
  sbo_vector(const sbo_vector&) = delete;
  sbo_vector& operator=(const sbo_vector&) = delete;

  constexpr ~sbo_vector() noexcept
  {
    assert(is_small());
    clear(); // call destructors
  }

  struct big_t
  {
    T* start;
    T* finish;
    // invariant: start <= finish && (finish - start) <= (storage_capacity_or_size >> 1) && (storage_capacity_or_size & 1) == 0
  };

  static constexpr std::size_t small_capacity = sizeof(big_t) / sizeof(T);

  struct empty_t {};
  union small_t
  {
    constexpr ~small_t() noexcept {} // NOP: owning class handles destruction via clear()

    empty_t empty = {};
    T val;
    // invariant: (storage_capacity_or_size & 1) && (storage_capacity_or_size >> 1) <= small_capacity
  };

  union {
    small_t small[small_capacity];
    big_t big;
  };
  // if ((storage_capacity_or_size & 1) == 0) size     := (storage_capacity_or_size >> 1)
  //                                     else capacity := (storage_capacity_or_size >> 1)
  std::size_t storage_capacity_or_size = 1;

  constexpr bool is_small() const noexcept
  {
    return this->storage_capacity_or_size & 1;
  }

  constexpr std::size_t capacity() const noexcept
  {
    if (is_small())
      return small_capacity;
    else
      return this->storage_capacity_or_size >> 1u;
  }

  constexpr std::size_t size() const noexcept
  {
    if (is_small())
      return this->storage_capacity_or_size >> 1u;
    else
      return this->big.finish - this->big.start;
  }

  template <typename Self>
  constexpr auto begin(this Self&& self) noexcept -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>
  {
    if (self.is_small())
      return &self.small[0].val;
    else
      return self.big.start;
  }

  template <typename Self>
  constexpr auto end(this Self&& self) noexcept -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>
  {
    if (self.is_small())
      return std::forward<Self>(self).begin() + self.size();
    else
      return self.big.finish;
  }

  template <typename Allocator>
  constexpr expected<void> reserve(std::size_t count, Allocator& alloc) noexcept(
      std::is_nothrow_move_constructible_v<T>
   || (std::is_nothrow_default_constructible_v<T>
    && std::is_nothrow_swappable_v<T>))
  {
    if (count <= capacity())
      return {};

    // Overflow either in byte-count or item-count
    if (count > (std::size_t(-1) >> 1u)
     || count > (std::size_t(-1) / sizeof(T)))
      return {unexpect, make_error_code(std::errc::result_out_of_range)};

    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
    using traits_t = std::allocator_traits<allocator_type>;
    allocator_type alloc_(alloc);
    if (count > traits_t::max_size(alloc_))
      return {unexpect, make_error_code(std::errc::not_enough_memory)};

    const auto [ptr, size] = [&alloc_, count]()
#if __cpp_lib_allocate_at_least >= 202306L
      -> std::allocation_result<typename traits_t::pointer, typename traits_t::size_type>
#else
      -> std::pair<typename traits_t::pointer, typename traits_t::size_type>
#endif
    {
      try
      {
#if __cpp_lib_allocate_at_least >= 202306L
        return traits_t::allocate_at_least(alloc_, count);
#else
        return {traits_t::allocate(alloc_, count), count};
#endif
      }
      catch (const std::bad_alloc&)
      {
        return {nullptr, 0};
      }
    }();

    if (ptr == nullptr)
      return {unexpect, make_error_code(std::errc::not_enough_memory)};

    const auto first = begin();
    const auto last = end();
    auto new_end = ptr;
    for (auto i = first; i != last; ++i, ++new_end)
    {
      if constexpr (std::is_nothrow_move_constructible_v<T>)
      {
        std::uninitialized_construct_using_allocator(new_end, alloc_, std::move(*i));
      }
      else if constexpr (std::is_nothrow_default_constructible_v<T>
                      && std::is_nothrow_swappable_v<T>)
      {
        using std::swap;
        std::uninitialized_construct_using_allocator(new_end, alloc_);
        swap(*new_end, *i);
      }
      else
      {
        try
        {
          std::uninitialized_construct_using_allocator(new_end, alloc_, std::move(*i));
        }
        catch (...)
        {
          // Make an effort to move back the already moved elements for exception safety.
          // But lets not care if *that* fails with another exception.
          for (; i != first; --i, --new_end)
          {
            *(new_end - 1) = std::move(*(i - 1));
            traits_t::destroy(alloc_, i - 1);
          }
          traits_t::deallocate(alloc_, ptr, size);
          throw;
        }
      }
    }
    for (auto i = first; i != last; ++i)
      traits_t::destroy(alloc_, i);
    if (!is_small())
    {
      ASAN_POISON_MEMORY_REGION(first, capacity() * sizeof(*first));
      traits_t::deallocate(alloc_, first, capacity());
    }

    this->big.start = ptr;
    this->big.finish = new_end;
    this->storage_capacity_or_size = (size << 1u) | 0u;
    ASAN_POISON_MEMORY_REGION(new_end, (size - (new_end - ptr)) * sizeof(*new_end));

    return {};
  }

  template <typename Allocator>
  constexpr expected<void> push_back(T el, Allocator& alloc) noexcept(
      noexcept(this->reserve(0, alloc)))
  {
    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
    allocator_type alloc_(alloc);

    assert(this->size() <= this->capacity());
    if (this->size() == this->capacity())
    {
      if (auto r = reserve(capacity() * 2, alloc);
          !r)
        return {unexpect, r.error()};
    }
    assert(this->size() < this->capacity());

    if (this->is_small())
    {
      std::uninitialized_construct_using_allocator(this->end(), alloc_, std::move(el));
      this->storage_capacity_or_size += 2u;
      assert(this->storage_capacity_or_size & 1u);
      return {};
    }

    ASAN_UNPOISON_MEMORY_REGION(big.finish, sizeof(*big.finish));
    std::uninitialized_construct_using_allocator(big.finish, alloc_, std::move(el));
    ++big.finish;

    return {};
  }

  constexpr T* erase(T* const first, T* last) noexcept(
      std::is_nothrow_move_assignable_v<T>
   && std::is_nothrow_default_constructible_v<T>
   && std::is_nothrow_destructible_v<T>)
  {
    const auto the_start = this->begin();
    const auto the_end = this->end();

    assert(the_start <= first);
    assert(last <= the_end);

    last = std::move(last, the_end, first);
    std::destroy_n(last, the_end - last);
    ASAN_POISON_MEMORY_REGION(last, (the_end - last) * sizeof(*the_end));

    if (this->is_small())
      this->storage_capacity_or_size = (static_cast<std::size_t>(last - the_start) << 1u) | 1u;
    else
      big.finish = last;
    return last;
  }

  friend constexpr decltype(auto) erase(sbo_vector& v, const T& el) noexcept(
      std::is_nothrow_move_constructible_v<T>
   && std::is_nothrow_move_assignable_v<T>
   && std::is_nothrow_default_constructible_v<T>
   && std::is_nothrow_destructible_v<T>)
  {
    const auto last = v.end();
    return v.erase(
        std::remove(v.begin(), last, el)
      , last
      );
  }

  constexpr T* erase(T* const pos) noexcept(
      std::is_nothrow_move_assignable_v<T>
   && std::is_nothrow_default_constructible_v<T>
   && std::is_nothrow_destructible_v<T>)
  {
    return erase(pos, pos + 1);
  }

  constexpr void clear() noexcept(std::is_nothrow_destructible_v<T>)
  {
    erase(begin(), end());
  }

  template <typename Allocator>
  constexpr void destroy(Allocator& alloc)
  {
    clear();

    if (!is_small())
    {
      using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
      allocator_type alloc_(alloc);
      std::allocator_traits<allocator_type>::deallocate(alloc_, big.start, this->capacity());
      this->storage_capacity_or_size = (0u << 1u) | 1u;
      std::destroy_at(&big.start);
      std::destroy_at(&big.finish);
    }

    assert(is_small());
  }
};
}  // namespace olifilo::detail
