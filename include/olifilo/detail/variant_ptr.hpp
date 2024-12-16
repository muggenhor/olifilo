// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024  Giel van Schijndel

#pragma once

#if __cpp_constexpr >= 202306L
#  define HAVE_CONSTEXPR_CAST_FROM_VOID 1
#  if defined(__has_builtin)
#    if __has_builtin(__builtin_tag_pointer_mask) && __has_builtin(__builtin_tag_pointer_mask_or) && __has_builtin(__builtin_tag_pointer_mask_as_int)
#      define HAS_CONSTEXPR_POINTER_TAGGING_P3125 1
#    endif
#  endif
#endif

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace olifilo::detail
{
// Necessary union packing helper to be constexpr on C++23
template <typename... Ts>
union pack_union;

template <>
union pack_union<>
{
  template <typename... Args>
  pack_union(Args&&...) = delete;
};

template <typename T, typename... Ts>
union pack_union<T, Ts...>
{
  using head_t = T;
  using tail_t = pack_union<Ts...>;

  template <typename... Args>
  constexpr pack_union(std::integral_constant<std::size_t, 0>, Args&&... args) noexcept(
      std::is_nothrow_constructible_v<head_t, Args&&...>)
    : head(std::forward<Args>(args)...)
  {
  }

  template <std::size_t Idx, typename... Args>
  constexpr pack_union(std::integral_constant<std::size_t, Idx>, Args&&... args) noexcept(
      std::is_nothrow_constructible_v<std::integral_constant<std::size_t, Idx -1>, tail_t, Args&&...>)
    : tail(std::integral_constant<std::size_t, Idx - 1>(), std::forward<Args>(args)...)
  {
  }

  head_t head;
  tail_t tail;

  template <std::size_t Idx>
  constexpr auto get() const noexcept
  {
    if constexpr (Idx == 0)
      return head;
    else
      return tail.template get<Idx - 1>();
  }
};

template <typename E, typename... Ts>
constexpr std::size_t find_type_index = 0;

template <typename E, typename T, typename... Ts>
requires(!std::is_same_v<E, T>)
constexpr std::size_t find_type_index<E, T, Ts...> = find_type_index<E, Ts...> + 1;

template <typename... Ts>
requires(0 < sizeof...(Ts)
          && sizeof...(Ts) <= std::min({alignof(Ts)...})
    && std::has_single_bit(std::min({alignof(Ts)...})))
class variant_ptr
{
#if HAVE_CONSTEXPR_CAST_FROM_VOID
    using storage_t = void*;
#else
    using storage_t = pack_union<std::add_pointer_t<Ts>...>;
#endif
    static constexpr std::uintptr_t mask = std::bit_ceil(static_cast<std::uintptr_t>(sizeof...(Ts))) - 1u;

  public:
    constexpr variant_ptr() noexcept
      : _storage(
#if !HAVE_CONSTEXPR_CAST_FROM_VOID
          std::integral_constant<std::size_t, 0>(),
#endif
          nullptr)
    {
    }

    constexpr variant_ptr(std::nullptr_t) noexcept
      : variant_ptr()
    {
    }

    template <typename T>
      requires(find_type_index<T, Ts...> < sizeof...(Ts))
    constexpr variant_ptr(T* ptr) noexcept
      : _storage(
#if !HAVE_CONSTEXPR_CAST_FROM_VOID
          std::integral_constant<std::size_t, find_type_index<T, Ts...>>(),
#endif
          [ptr]() {
            constexpr auto idx = find_type_index<T, Ts...>;
#if HAS_CONSTEXPR_POINTER_TAGGING_P3125
            return static_cast<
#if HAVE_CONSTEXPR_CAST_FROM_VOID
              void*
#else
              T*
#endif
              >(__builtin_tag_pointer_mask_or(const_cast<void*>(static_cast<const void*>(ptr)), idx, mask));
#else
              // Special case to make initializing with pointers to the first type constexpr-safe
            if consteval
            {
#if HAVE_CONSTEXPR_CAST_FROM_VOID
              if (idx == 0)
                return const_cast<void*>(static_cast<const void*>(ptr));
#else
              if (idx == 0)
                return ptr;
#endif
            }

            return std::bit_cast<
#if HAVE_CONSTEXPR_CAST_FROM_VOID
              void*
#else
              T*
#endif
              >((std::bit_cast<std::uintptr_t>(ptr) & ~mask) | (idx & mask));
#endif
          }())
    {
      if !consteval
      {
        assert((std::bit_cast<std::uintptr_t>(ptr) & mask) == 0);
      }
    }

    template <std::size_t Idx = 0>
    constexpr std::uintptr_t index() const noexcept
    {
#if HAS_CONSTEXPR_POINTER_TAGGING_P3125
      return __builtin_tag_pointer_mask_as_int(_storage, mask);
#else
      // Special case to make default initialized constexpr-safe
      if consteval
      {
#  if HAVE_CONSTEXPR_CAST_FROM_VOID
        if (_storage == nullptr)
          return 0;
#  else
        if (_storage.template get<Idx>() == nullptr)
          return 0;
#  endif
      }

      return std::bit_cast<std::uintptr_t>(_storage) & mask;
#endif
    }

    template <typename T>
      requires(std::is_pointer_v<T>
            && find_type_index<T, std::add_pointer_t<Ts>...> < sizeof...(Ts))
    friend constexpr bool contains(const variant_ptr& var) noexcept
    {
      constexpr auto idx = find_type_index<T, std::add_pointer_t<Ts>...>;

      return var.index<idx>() == idx;
    }

    template <typename T>
      requires(!std::is_pointer_v<T>
            || find_type_index<T, std::add_pointer_t<Ts>...> >= sizeof...(Ts))
    friend consteval bool contains(const variant_ptr&) noexcept
    {
      return false;
    }

    template <typename T>
      requires(std::is_pointer_v<T>
            && find_type_index<T, std::add_pointer_t<Ts>...> < sizeof...(Ts))
    friend constexpr T get(const variant_ptr& var) noexcept
    {
      constexpr auto idx = find_type_index<T, std::add_pointer_t<Ts>...>;

#if !HAS_CONSTEXPR_POINTER_TAGGING_P3125
      if !consteval
#endif
      {
        assert(var.index<idx>() == idx);
      }
#if HAS_CONSTEXPR_POINTER_TAGGING_P3125
      return static_cast<T>(__builtin_tag_pointer_mask(var._storage, ~mask));
#else
#  if HAVE_CONSTEXPR_CAST_FROM_VOID
      const auto ptr = var._storage;
#  else
      const T ptr = var._storage.template get<idx>();
#  endif
      if consteval
      {
        // Special case for avoiding static_cast<T>(nullptr) in constexpr context
        if (ptr == nullptr)
          return nullptr;
        // Special case for avoiding bit masking in constexpr context
        if (idx == 0)
          return static_cast<T>(ptr);
      }

      return std::bit_cast<T>(std::bit_cast<std::uintptr_t>(ptr) & ~mask);
#endif
    }

    template <std::size_t N>
      requires(N < sizeof...(Ts))
    friend constexpr auto get(const variant_ptr& var) noexcept
    {
      using type = std::remove_cvref_t<decltype(std::declval<pack_union<std::add_pointer_t<Ts>...>>().template get<N>())>;
      return get<type>(var);
    }

    template <typename F>
    friend constexpr decltype(auto) visit(F&& visitor, const variant_ptr& var) noexcept
    {
      static_assert(sizeof...(Ts) <= 8, "unimplemented: only support visiting variants with up to 8 types");

      switch (var.index())
      {
        case 0:
          if constexpr (0 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<0>(var));
        case 1:
          if constexpr (1 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<1>(var));
        case 2:
          if constexpr (2 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<2>(var));
        case 3:
          if constexpr (3 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<3>(var));
        case 4:
          if constexpr (4 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<4>(var));
        case 5:
          if constexpr (5 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<5>(var));
        case 6:
          if constexpr (6 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<6>(var));
        case 7:
          if constexpr (7 < sizeof...(Ts))
            return std::invoke(std::forward<F>(visitor), get<7>(var));
        default:
          assert(var.index() < 8 && "index out of range");
          std::unreachable();
      }
    }

    explicit constexpr operator bool() const noexcept
    {
#if HAS_CONSTEXPR_POINTER_TAGGING_P3125
      return __builtin_tag_pointer_mask(_storage, ~mask) != nullptr;
#else
      if consteval
      {
#if HAVE_CONSTEXPR_CAST_FROM_VOID
        const auto ptr = _storage;
#else
        const auto ptr = _storage.template get<0>();
#endif
        // Special case for avoiding bit masking in constexpr context
        if (ptr == nullptr)
          return false;
      }

      return (std::bit_cast<std::uintptr_t>(_storage) & ~mask) != 0;
#endif
    }

    constexpr bool operator==(std::nullptr_t) const noexcept
    {
      return !*this;
    }

    constexpr bool operator==(const variant_ptr& rhs) const noexcept
    {
      return std::bit_cast<std::uintptr_t>(_storage) == std::bit_cast<std::uintptr_t>(rhs._storage);
    }

    template <typename T>
      requires(find_type_index<T, Ts...> < sizeof...(Ts))
    constexpr bool operator==(T* ptr) const noexcept
    {
      constexpr auto idx = find_type_index<T, Ts...>;

      return index<idx>() == idx && get<idx>(*this) == ptr;
    }

    template <typename T>
      requires(find_type_index<T, Ts...> >= sizeof...(Ts)
            && find_type_index<std::remove_cv_t<T>, std::remove_cv_t<Ts>...> < sizeof...(Ts))
    constexpr bool operator==(T* ptr) const noexcept
    {
      constexpr auto idx = find_type_index<std::remove_cv_t<T>, std::remove_cv_t<Ts>...>;

      return index<idx>() == idx && get<idx>(*this) == ptr;
    }

  private:
    storage_t _storage;
};

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

#if HAS_CONSTEXPR_POINTER_TAGGING_P3125
#  define tag_assert(x) static_assert(x)
#  define tag_constexpr constexpr
#elif NDEBUG
#  define tag_assert(x) [[assume(x)]]
#  define tag_constexpr const
#else
#  define tag_assert(x) assert(x)
#  define tag_constexpr const
#endif

constexpr void test_variant_ptr()
{
  using test_ptr_t = variant_ptr<int, void* const, const float>;
  static_assert(std::is_standard_layout_v<test_ptr_t>);

  static constexpr auto is_nullptr = [] (auto p) { return p == nullptr; };
  static constexpr auto is_int_ptr = []<typename T>(T*) { return std::is_same_v<T, int>; };
  static constexpr auto is_voidpc_ptr = []<typename T>(T*) { return std::is_same_v<T, void* const>; };
  static constexpr auto is_the_answer = overloaded{
      []<typename T> requires(std::is_arithmetic_v<T>) (T* p) {
        return p != nullptr && *p == 42;
      },
      [] (auto) {
        return false;
      },
    };

  static constexpr test_ptr_t test_default_ptr;
  static_assert(test_default_ptr.index() == 0);
  static_assert(contains<int*>(test_default_ptr));
  static_assert(get<int*>(test_default_ptr) == nullptr);
  static_assert(visit(is_nullptr, test_default_ptr));
  static_assert(test_default_ptr == nullptr);

  static constinit int test_int = 42;
  static constexpr test_ptr_t test_int_ptr(&test_int);
  tag_assert(contains<int*>(test_int_ptr));
  tag_assert(get<int*>(test_int_ptr) == &test_int); // should be static_assert even on C++23 but GCC complains *sometimes*
  static tag_constexpr int& int_ref = *get<int*>(test_int_ptr); // should be constexpr even on C++23 but GCC complains *sometimes*
  tag_assert(&int_ref == &test_int); // should be static_assert even on C++23 but GCC complains *sometimes*
  tag_assert(visit(is_int_ptr, test_int_ptr));
  tag_assert(test_int_ptr != nullptr);

  static constexpr void* test_void_ptr = nullptr;
  static tag_constexpr test_ptr_t test_void_ptr_ptr(&test_void_ptr);
  tag_assert(test_void_ptr_ptr.index() == 1);
  tag_assert(contains<void* const*>(test_void_ptr_ptr));
  tag_assert(get<void* const*>(test_void_ptr_ptr) == &test_void_ptr);
  tag_assert(*get<void* const*>(test_void_ptr_ptr) == nullptr);
  tag_assert(visit(is_voidpc_ptr, test_void_ptr_ptr));
  tag_assert(test_void_ptr_ptr != nullptr);

  static constexpr auto test_copy_int_ptr = test_int_ptr;
  tag_assert(contains<int*>(test_copy_int_ptr));
  tag_assert(get<int*>(test_copy_int_ptr) == &test_int); // should be static_assert even on C++23 but GCC complains *sometimes*
  tag_assert(test_copy_int_ptr != nullptr);

  // Test mutation (copy assignment)
  test_ptr_t test_ptr;
  assert(test_ptr.index() == 0);
  assert(contains<int*>(test_ptr));
  assert(get<int*>(test_ptr) == nullptr);
  assert(nullptr == test_ptr);
  assert(visit(is_nullptr, test_ptr));
  assert(!visit(is_the_answer, test_ptr));

  test_ptr = test_void_ptr_ptr;
  assert(test_ptr.index() == 1);
  assert(contains<void* const*>(test_ptr));
  assert(get<void* const*>(test_ptr) == &test_void_ptr);
  assert(nullptr != test_ptr);
  assert(visit(is_voidpc_ptr, test_ptr));
  assert(!visit(is_the_answer, test_ptr));

  test_ptr = static_cast<void* const*>(nullptr);
  assert(test_ptr.index() == 1);
  assert(contains<void* const*>(test_ptr));
  assert(get<void* const*>(test_ptr) == nullptr);
  assert(nullptr == test_ptr);
  assert(visit(is_voidpc_ptr, test_ptr));
  assert(!visit(is_the_answer, test_ptr));
  assert(test_default_ptr != test_ptr && test_default_ptr == nullptr && test_ptr == nullptr); // two different types of null

  test_ptr = test_int_ptr;
  assert(test_ptr.index() == 0);
  assert(contains<int*>(test_ptr));
  assert(get<int*>(test_ptr) == &test_int);
  assert(nullptr != test_ptr);
  assert(visit(is_int_ptr, test_ptr));
  assert(visit(is_the_answer, test_ptr));

  test_ptr = test_default_ptr;
  assert(test_ptr.index() == 0);
  assert(contains<int*>(test_ptr));
  assert(get<int*>(test_ptr) == nullptr);
  assert(nullptr == test_ptr);
  assert(visit(is_nullptr, test_ptr));
  assert(!visit(is_the_answer, test_ptr));

  static constexpr float test_float = 42.f;
  test_ptr = &test_float;
  assert(test_ptr.index() == 2);
  assert(contains<const float*>(test_ptr));
  assert(get<const float*>(test_ptr) == &test_float);
  assert(nullptr != test_ptr);
  assert(visit(is_the_answer, test_ptr));

  static constinit const int test_const_int = 21;
  tag_assert(test_int_ptr != &test_const_int);
}
}  // namespace olifilo::detail

#undef HAVE_CONSTEXPR_CAST_FROM_VOID
#undef HAS_CONSTEXPR_POINTER_TAGGING_P3125
