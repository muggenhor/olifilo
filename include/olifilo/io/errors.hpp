// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <system_error>

namespace olifilo::io
{
enum class error
{
  operation_not_ready = 1,
};

struct error_category_t : std::error_category
{
  const char* name() const noexcept override;
  std::string message(int ev) const override;

  constexpr bool equivalent(const std::error_code& ec, int cond) const noexcept override
  {
    switch (static_cast<error>(cond))
    {
      case error::operation_not_ready:
        if (ec.category() != std::system_category()
         && ec.category() != std::generic_category())
          return false;

        const auto errc = static_cast<std::errc>(ec.value());
        return (errc == std::errc::resource_unavailable_try_again
             || errc == std::errc::operation_would_block
             || errc == std::errc::operation_in_progress);
    }

    return false;
  }
};

constexpr const error_category_t& error_category() noexcept
{
  static error_category_t cat;
  return cat;
}

inline std::error_condition make_error_condition(error e)
{
  return {static_cast<int>(e), error_category()};
}
}  // namespace olifilo::io

template <>
struct std::is_error_condition_enum<olifilo::io::error> : true_type {};
