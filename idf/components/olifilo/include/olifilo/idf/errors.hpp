// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <system_error>

namespace olifilo::esp
{
struct error_category_t : std::error_category
{
  const char* name() const noexcept override;
  std::string message(int ev) const override;
  bool equivalent(int code, const std::error_condition& condition) const noexcept override;
};

constexpr const error_category_t& error_category() noexcept
{
  static error_category_t cat;
  return cat;
}
}  // namespace olifilo::esp
