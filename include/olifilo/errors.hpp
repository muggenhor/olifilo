// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <system_error>

namespace olifilo
{
enum class condition
{
  operation_not_ready = 1,
};

struct condition_category_t : std::error_category
{
  const char* name() const noexcept override;
  std::string message(int ev) const override;

  constexpr bool equivalent(const std::error_code& ec, int cond) const noexcept override
  {
    switch (static_cast<condition>(cond))
    {
      case condition::operation_not_ready:
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

constexpr const condition_category_t& condition_category() noexcept
{
  static condition_category_t cat;
  return cat;
}

inline std::error_condition make_error_condition(condition e)
{
  return {static_cast<int>(e), condition_category()};
}
}  // namespace olifilo

template <>
struct std::is_error_condition_enum<olifilo::condition> : true_type {};
