// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include <version>

#if __cpp_lib_expected >= 202202L
#  include <expected>
#endif

namespace olifilo
{
template <typename T>
class expected;

template <typename>
struct is_expected : std::false_type {};

template <typename T>
struct is_expected<expected<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_expected_v = is_expected<T>::value;

template <typename>
struct is_expected_with_std_error_code : std::false_type {};

template <typename T>
struct is_expected_with_std_error_code<expected<T>> : std::true_type {};

#if __cpp_lib_expected >= 202202L
template <typename T>
struct is_expected_with_std_error_code<std::expected<T, std::error_code>> : std::true_type {};
#endif

template <typename T>
inline constexpr bool is_expected_with_std_error_code_v = is_expected_with_std_error_code<T>::value;

struct unexpect_t
{
  unexpect_t() = default;

#if __cpp_lib_expected >= 202202L
  explicit(false) constexpr unexpect_t(std::unexpect_t) noexcept;
  explicit(false) constexpr operator std::unexpect_t() const noexcept { return std::unexpect; }
#endif
};

template <typename T>
struct unexpected
{
  T _value;

  template <typename Self>
  constexpr decltype(auto) error(this Self&& self) noexcept
  {
    return std::forward_like<Self>(self._value);
  }

#if __cpp_lib_expected >= 202202L
  template <typename Self, typename U>
  explicit(!std::is_convertible_v<decltype(std::forward_like<Self>(std::declval<T>())), U>)
  constexpr operator std::unexpected<U>(this Self&& self)
    noexcept(std::is_nothrow_constructible_v<U, decltype(std::forward_like<Self>(std::declval<T>()))>)
    requires(std::is_constructible_v<U, decltype(std::forward_like<Self>(std::declval<T>()))>)
  {
    return std::unexpected<T>(std::in_place, std::forward_like<Self>(self._value));
  }
#endif
};

#if __cpp_lib_expected >= 202202L
static_assert(std::is_convertible_v<unexpected<int>, std::unexpected<int>>);
static_assert(std::is_constructible_v<std::unexpected<int>, unexpected<int>>);
#endif

inline constexpr unexpect_t unexpect;

namespace detail
{
template <typename T>
struct expected_storage
{
  int _error = 0;
  union {
    const std::error_category* _error_cat;
    T _value;
  };

  template <typename... Args>
  constexpr expected_storage(std::in_place_t, Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args&&...>)
    requires(std::is_constructible_v<T, Args&&...>)
    : _error(0)
    , _value(std::forward<Args>(args)...)
  {
  }

  constexpr expected_storage(unexpect_t, int code, const std::error_category& category) noexcept
    : _error(code)
    , _error_cat(&category)
  {
    if constexpr (std::is_nothrow_default_constructible_v<T>)
    {
      if (code == 0)
        std::construct_at(&_value);
    }
    else
    {
      assert(code != 0 || "value type isn't nothrow default constructible!");
    }
  }

  constexpr ~expected_storage()
    noexcept(std::is_nothrow_destructible_v<T>)
    requires(std::is_destructible_v<T>)
  {
    if constexpr (!std::is_trivially_destructible_v<T>)
    {
      if (_error == 0)
        std::destroy_at(&_value);
    }
  }

  constexpr expected_storage(const expected_storage& rhs)
    noexcept(std::is_nothrow_copy_constructible_v<T>)
    requires(std::is_copy_constructible_v<T>)
  {
    if (rhs._error)
    {
      _error = rhs._error;
      _error_cat = rhs._error_cat;
    }
    else
    {
      std::construct_at(&_value, rhs._value);
      _error = 0;
    }
  }

  constexpr expected_storage(expected_storage&& rhs)
    noexcept(std::is_nothrow_move_constructible_v<T>)
    requires(std::is_move_constructible_v<T>)
  {
    if (rhs._error)
    {
      _error = rhs._error;
      _error_cat = rhs._error_cat;
    }
    else
    {
      std::construct_at(&_value, std::move(rhs._value));
      _error = 0;
    }
  }

  constexpr expected_storage& operator=(expected_storage&& rhs)
    noexcept(std::is_nothrow_move_assignable_v<T>
          && std::is_nothrow_move_constructible_v<T>
          && std::is_nothrow_destructible_v<T>)
    requires(std::is_move_assignable_v<T>
          && std::is_move_constructible_v<T>
          && std::is_destructible_v<T>)
  {
    if (rhs._error)
    {
      if constexpr (!std::is_trivially_destructible_v<T>)
        if (!_error)
          std::destroy_at(&_value);

      _error = rhs._error;
      _error_cat = rhs._error_cat;
    }
    else
    {
      if (_error)
      {
        std::construct_at(&_value, std::move(rhs._value));
        _error = 0;
      }
      else
      {
        _value = std::move(rhs._value);
      }
    }

    return *this;
  }

  constexpr expected_storage& operator=(const expected_storage& rhs)
    noexcept(std::is_nothrow_copy_assignable_v<T>
          && std::is_nothrow_copy_constructible_v<T>
          && std::is_nothrow_destructible_v<T>)
    requires(std::is_copy_assignable_v<T>
          && std::is_copy_constructible_v<T>
          && std::is_destructible_v<T>)
  {
    if (rhs._error)
    {
      if constexpr (!std::is_trivially_destructible_v<T>)
        if (_error)
          std::destroy_at(&_value);

      _error = rhs._error;
      _error_cat = rhs._error_cat;
    }
    else
    {
      if (_error)
      {
        std::construct_at(&_value, rhs._value);
        _error = 0;
      }
      else
      {
        _value = rhs._value;
      }
    }

    return *this;
  }
};

template <>
struct expected_storage<void>
{
  int _error = 0;
  struct empty_t {
#if __GNUC__ && (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) <= 140200
    // workaround for https://gcc.gnu.org/bugzilla/show_bug.cgi?id=118107
    // make non-empty on GCC <= 14.2 because it incorrectly causes warnings about the *inactive* member being uninitialized
    char _;
#endif
  };

  union {
    const std::error_category* _error_cat;
    empty_t _value = {};
  };

  constexpr expected_storage() noexcept = default;
  constexpr expected_storage(std::in_place_t) noexcept {}

  constexpr expected_storage(unexpect_t, int code, const std::error_category& category) noexcept
    : _error(code)
    , _error_cat(&category)
  {
  }
};
} // namespace detail

template <typename T>
class [[nodiscard("error silently ignored")]] expected
{
  public:
    constexpr expected(const expected& rhs)
      noexcept(std::is_same_v<T, void> || std::is_nothrow_copy_constructible_v<T>)
      requires(std::is_same_v<T, void> || std::is_copy_constructible_v<T>) = default;

    constexpr expected(expected&& rhs)
      noexcept(std::is_same_v<T, void> || std::is_nothrow_move_constructible_v<T>)
      requires(std::is_same_v<T, void> || std::is_move_constructible_v<T>) = default;

    constexpr expected& operator=(const expected& rhs)
      noexcept(std::is_same_v<T, void>
            || (std::is_nothrow_copy_assignable_v<T>
             && std::is_nothrow_copy_constructible_v<T>
             && std::is_nothrow_destructible_v<T>))
      requires(std::is_same_v<T, void>
            || (std::is_copy_assignable_v<T>
             && std::is_copy_constructible_v<T>
             && std::is_destructible_v<T>)) = default;

    constexpr expected& operator=(expected&& rhs)
      noexcept(std::is_same_v<T, void>
            || (std::is_nothrow_move_assignable_v<T>
             && std::is_nothrow_move_constructible_v<T>
             && std::is_nothrow_destructible_v<T>))
      requires(std::is_same_v<T, void>
            || (std::is_move_assignable_v<T>
             && std::is_move_constructible_v<T>
             && std::is_destructible_v<T>)) = default;

    constexpr expected()
      noexcept(std::is_same_v<T, void> || std::is_nothrow_default_constructible_v<T>)
      requires(std::is_same_v<T, void> || std::is_default_constructible_v<T>)
      : expected(std::in_place)
    {
    }

    constexpr expected(unexpect_t unexpect, int code, const std::error_category& category) noexcept
      : _storage(unexpect, code, category)
    {
    }

    constexpr expected(unexpect_t unexpect, std::error_code error) noexcept
      : _storage(unexpect, error.value(), error.category())
    {
    }

    constexpr expected(unexpected<std::error_code> error) noexcept
      : expected(unexpect, error.error())
    {
    }

    constexpr expected(std::error_code error) noexcept
      requires(!std::is_constructible_v<T, std::error_code&>
            && !std::is_constructible_v<T, std::error_code&&>)
      : _storage(unexpect, error.value(), error.category())
    {
    }

    template <typename... Args>
    constexpr expected(std::in_place_t in_place, Args&&... args)
      noexcept(std::is_nothrow_constructible_v<detail::expected_storage<T>, std::in_place_t, Args&&...>)
      requires(std::is_constructible_v<detail::expected_storage<T>, std::in_place_t, Args&&...>)
      : _storage(in_place, std::forward<Args>(args)...)
    {
    }

    template <typename U = T>
    explicit(!std::is_convertible_v<U&&, T>)
    constexpr expected(U&& value)
      noexcept(std::is_nothrow_constructible_v<T, U&&>)
      requires(!std::is_same_v<T, void>
            && std::is_constructible_v<T, U&&>
            && !std::is_same_v<std::decay_t<U>, std::in_place_t>
            && !std::is_same_v<std::decay_t<U>, expected<T>>
            && !is_expected_v<std::decay_t<U>>)
      : _storage(std::in_place, std::forward<U>(value))
    {
    }

    template <typename U>
      requires((std::is_same_v<T, void>
             || std::is_constructible_v<T, const U&>)
            && (!std::is_same_v<std::remove_cv_t<T>, bool>
             || !is_expected_v<std::decay_t<U>>))
    explicit(!std::is_same_v<T, void>
          && !std::is_convertible_v<const U&, T>)
    constexpr expected(const expected<U>& rhs)
      noexcept(std::is_same_v<T, void>
            || std::is_nothrow_constructible_v<T, const U&>)
    {
      if (rhs)
      {
        std::construct_at(&_storage._value, *rhs);
      }
      else
      {
        const auto error = rhs.error();
        _storage._error = error.value();
        _storage._error_cat = &error.category();
      }
    }

    template <typename U>
      requires(std::is_same_v<T, void>
            || std::is_constructible_v<T, U&&>)
    explicit(!std::is_same_v<T, void>
          && !std::is_convertible_v<U&&, T>)
    constexpr expected(expected<U>&& rhs)
      noexcept(std::is_same_v<T, void>
            || std::is_nothrow_constructible_v<T, U&&>)
    {
      if (rhs)
      {
        if constexpr (!std::is_same_v<T, void>)
          std::construct_at(&_storage._value, std::move(*rhs));
      }
      else
      {
        const auto error = rhs.error();
        _storage._error = error.value();
        _storage._error_cat = &error.category();
      }
    }

    template <typename... Args>
      requires((std::is_same_v<T, void> && sizeof...(Args) == 0)
            || std::is_nothrow_constructible_v<T, Args...>)
    constexpr decltype(auto) emplace(Args&&... args)
      noexcept(std::is_same_v<T, void>
            || std::is_nothrow_destructible_v<T>)
    {
      if constexpr (!std::is_same_v<T, void>
                 && !std::is_trivially_destructible_v<T>)
      {
        if (*this)
          std::destroy_at(&_storage._value);
      }

      _storage._error = 0;

      if constexpr (!std::is_same_v<T, void>)
        return *std::construct_at(&_storage._value, std::forward<Args>(args)...);
    }

#if __cpp_lib_expected >= 202202L
    template <typename Self>
    constexpr operator std::expected<T, std::error_code>(this Self&& self)
      noexcept(std::is_same_v<T, void>
            || std::is_nothrow_constructible_v<T, decltype(*std::declval<Self>())>)
      requires(std::is_same_v<T, void>
            || std::is_constructible_v<T, decltype(*std::declval<Self>())>)
    {
      using result_t = std::expected<T, std::error_code>;

      if (!self)
        return result_t(unexpect, self.error());

      if constexpr (std::is_same_v<T, void>)
        return result_t();
      else
        return result_t(*std::forward<Self>(self));
    }
#endif

    explicit constexpr operator bool() const noexcept
    {
      return _storage._error == 0;
    }

    constexpr bool has_value() const noexcept
    {
      return static_cast<bool>(*this);
    }

    template <typename Self>
    constexpr decltype(auto) operator*(this Self&& self) noexcept
    {
      assert(self && "dereferencing errored expected");
      if constexpr (!std::is_same_v<T, void>)
        return std::forward_like<Self>(self._storage._value);
      else
        return void();
    }

    template <typename Self>
    constexpr auto* operator->(this Self&& self) noexcept
      requires(!std::is_same_v<T, void>)
    {
      assert(self && "dereferencing errored expected");
      return &self._storage._value;
    }

    template <typename Self>
    constexpr decltype(auto) value(this Self&& self)
    {
      if (!self)
#if __cpp_exceptions
        throw std::system_error(self.error());
#else
      	std::abort();
#endif

      if constexpr (!std::is_same_v<T, void>)
        return std::forward_like<Self>(self._storage._value);
      else
        return void();
    }

    constexpr std::error_code error() const noexcept
    {
      if (*this)
        return std::error_code();

      return std::error_code(_storage._error, *_storage._error_cat);
    }

    template <std::invocable<> F>
      requires(std::is_same_v<T, void>
            && is_expected_v<std::remove_cvref_t<std::invoke_result_t<F&&>>>)
    constexpr auto and_then(F&& f) const
    {
      using result_t =
        std::remove_cvref_t<std::invoke_result_t<F&&>>
      ;

      if (!*this)
        return result_t(unexpect, this->error());

      return result_t(std::invoke(f));
    }

    template <std::invocable<> F>
      requires(std::is_same_v<T, void>)
    constexpr auto transform(F&& f) const
    {
      using result_t = expected<
        std::remove_cvref_t<std::invoke_result_t<F&&>>
      >;

      if (!*this)
        return result_t(unexpect, this->error());

      return result_t(std::invoke(f));
    }

    template <typename Self, std::invocable<decltype(*std::declval<Self>())> F>
      requires(!std::is_same_v<T, void>
            && is_expected_v<
                std::remove_cvref_t<
                  std::invoke_result_t<F&&, decltype(*std::declval<Self>())>
                >
               >)
    constexpr auto and_then(this Self&& self, F&& f)
    {
      using result_t =
        std::remove_cvref_t<std::invoke_result_t<F&&, decltype(*std::forward<Self>(self))>>
      ;

      if (!self)
        return result_t(unexpect, self.error());

      return result_t(std::invoke(f, *std::forward<Self>(self)));
    }

    template <typename Self, std::invocable<decltype(*std::declval<Self>())> F>
      requires(!std::is_same_v<T, void>)
    constexpr auto transform(this Self&& self, F&& f)
    {
      using result_t = expected<
        std::remove_cvref_t<std::invoke_result_t<F&&, decltype(*std::forward<Self>(self))>>
      >;

      if (!self)
        return result_t(unexpect, self.error());

      return result_t(std::invoke(f, *std::forward<Self>(self)));
    }

  private:
    detail::expected_storage<T> _storage;
};

template <typename T>
inline constexpr bool is_big_five_nothrow =
     std::is_nothrow_default_constructible_v<T>
  && std::is_nothrow_copy_constructible_v<T>
  && std::is_nothrow_copy_assignable_v<T>
  && std::is_nothrow_move_constructible_v<T>
  && std::is_nothrow_move_assignable_v<T>
  ;

template <typename T>
inline constexpr bool is_big_five_nothrow_move =
     std::is_nothrow_default_constructible_v<T>
  && std::is_copy_constructible_v<T>
  && std::is_copy_assignable_v<T>
  && std::is_nothrow_move_constructible_v<T>
  && std::is_nothrow_move_assignable_v<T>
  ;

template <typename T>
inline constexpr bool is_big_five_nothrow_move_only =
     std::is_nothrow_default_constructible_v<T>
  && !std::is_copy_constructible_v<T>
  && !std::is_copy_assignable_v<T>
  && std::is_nothrow_move_constructible_v<T>
  && std::is_nothrow_move_assignable_v<T>
  ;

static_assert(is_big_five_nothrow<expected<void>>);
static_assert(is_big_five_nothrow<expected<int>>);
static_assert(is_big_five_nothrow<expected<std::error_code>>);
static_assert(is_big_five_nothrow_move<expected<std::vector<int>>>);
static_assert(is_big_five_nothrow_move_only<expected<std::unique_ptr<int>>>);

static_assert(std::is_same_v<decltype(*std::declval<      expected<int>& >()),       int& >);
static_assert(std::is_same_v<decltype(*std::declval<const expected<int>& >()), const int& >);
static_assert(std::is_same_v<decltype(*std::declval<      expected<int>&&>()),       int&&>);
static_assert(std::is_same_v<decltype(*std::declval<const expected<int>&&>()), const int&&>);

static_assert(std::is_same_v<decltype(std::declval<      expected<int>& >().value()),       int& >);
static_assert(std::is_same_v<decltype(std::declval<const expected<int>& >().value()), const int& >);
static_assert(std::is_same_v<decltype(std::declval<      expected<int>&&>().value()),       int&&>);
static_assert(std::is_same_v<decltype(std::declval<const expected<int>&&>().value()), const int&&>);

static_assert(std::is_same_v<decltype(std::declval<      expected<int>>().operator->()),       int*>);
static_assert(std::is_same_v<decltype(std::declval<const expected<int>>().operator->()), const int*>);

static_assert(std::is_same_v<decltype(*std::declval<      expected<void>& >()), void>);
static_assert(std::is_same_v<decltype(*std::declval<const expected<void>& >()), void>);
static_assert(std::is_same_v<decltype(*std::declval<      expected<void>&&>()), void>);
static_assert(std::is_same_v<decltype(*std::declval<const expected<void>&&>()), void>);

static_assert(std::is_same_v<decltype(std::declval<      expected<void>& >().value()), void>);
static_assert(std::is_same_v<decltype(std::declval<const expected<void>& >().value()), void>);
static_assert(std::is_same_v<decltype(std::declval<      expected<void>&&>().value()), void>);
static_assert(std::is_same_v<decltype(std::declval<const expected<void>&&>().value()), void>);

inline template class expected<void>;
}
