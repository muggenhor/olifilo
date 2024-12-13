// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <type_traits>

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
requires(std::is_pointer_v<T>)
struct sbo_vector
{
  sbo_vector() = default;
  // we (currently) don't need copying so don't bother implementing it
  sbo_vector(const sbo_vector&) = delete;
  sbo_vector& operator=(const sbo_vector&) = delete;

  struct big_t
  {
    T* start;
    T* finish;
    // invariant: start && finish && end_of_storage && start <= finish <= end_of_storage
  };

  struct small_t
  {
    T vals[2];
    // invariant: end_of_storage == nullptr && (vals[0] || !vals[1])
  };

  union {
    small_t small = {};
    big_t big;
  };
  T* end_of_storage = nullptr;

  constexpr bool is_small() const noexcept
  {
    return end_of_storage == nullptr;
  }

  template <typename Self>
  constexpr auto begin(this Self&& self) noexcept -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>
  {
    if (self.is_small())
      return &self.small.vals[0];
    else
      return self.big.start;
  }

  template <typename Self>
  constexpr auto end(this Self&& self) noexcept -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>
  {
    if (self.is_small())
      return std::find(&self.small.vals[0], &self.small.vals[2], nullptr);
    else
      return self.big.finish;
  }

  template <typename Allocator>
  constexpr void reserve(std::size_t count, Allocator& alloc)
  {
    if (count <= 2)
      return;

    if (!is_small() && static_cast<std::size_t>(end_of_storage - big.start) >= count)
      return;

    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
    allocator_type alloc_(alloc);
#if __cpp_lib_allocate_at_least >= 202306L
    const auto [ptr, size] = std::allocator_traits<allocator_type>::allocate_at_least(alloc_, count);
#else
    const auto size = count;
    const auto ptr = std::allocator_traits<allocator_type>::allocate(alloc_, size);
#endif

    const auto first = begin();
    const auto last = end();
    const auto new_end = std::uninitialized_move(first, last, ptr);
    for (auto i = first; i != last; ++i)
      std::allocator_traits<allocator_type>::destroy(alloc_, i);
    if (!is_small())
    {
      ASAN_POISON_MEMORY_REGION(first, (end_of_storage - first) * sizeof(*last));
      std::allocator_traits<allocator_type>::deallocate(alloc_, first, end_of_storage - first);
    }

    big.start = ptr;
    big.finish = new_end;
    end_of_storage = ptr + size;
  }

  template <typename Allocator>
  constexpr void push_back(T el, Allocator& alloc)
  {
    assert(el != nullptr);

    if (!small.vals[0])
    {
      assert(end() - begin() == 0);
      small.vals[0] = el;
      return;
    }
    else if (!small.vals[1])
    {
      assert(end() - begin() == 1);
      small.vals[1] = el;
      return;
    }

    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
    allocator_type alloc_(alloc);
    if (is_small())
      reserve(4, alloc_);
    else if (big.finish == end_of_storage)
      reserve((end_of_storage - big.start) * 2, alloc_);

    assert(big.finish < end_of_storage);
    ASAN_UNPOISON_MEMORY_REGION(big.finish, sizeof(*big.finish));
    std::allocator_traits<allocator_type>::construct(alloc_, big.finish++, el);
  }

  constexpr T* erase(T* const first, T* last) noexcept(
      std::is_nothrow_move_assignable_v<T>
   && std::is_nothrow_default_constructible_v<T>
   && std::is_nothrow_destructible_v<T>)
  {
    if (is_small())
    {
      const auto the_end = end();
      last = std::move(last, the_end, first);
      std::fill(last, the_end, T{});
      return last;
    }

    last = std::move(last, big.finish, first);
    std::destroy_n(last, big.finish - last);
    ASAN_POISON_MEMORY_REGION(last, (big.finish - last) * sizeof(*big.finish));
    return big.finish = last;
  }

  friend constexpr decltype(auto) erase(sbo_vector& v, T el) noexcept(
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
      std::allocator_traits<allocator_type>::deallocate(alloc_, big.start, end_of_storage - big.start);
      end_of_storage = nullptr;
      std::uninitialized_default_construct(&small.vals[0], &small.vals[2]);
    }
  }
};
}  // namespace olifilo::detail
