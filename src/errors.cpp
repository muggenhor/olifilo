// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/errors.hpp>

namespace olifilo
{
const char* error_category_t::name() const noexcept
{
  return "olifilo-error";
}

std::string error_category_t::message(int ev) const
{
  switch (static_cast<error>(ev))
  {
    case error::uninitialized:
      return "uninitialized";
    case error::broken_promise:
      return "broken promise";
  }

  return "(unrecognized error)";
}

const char* condition_category_t::name() const noexcept
{
  return "olifilo-condition";
}

std::string condition_category_t::message(int ev) const
{
  switch (static_cast<condition>(ev))
  {
    case condition::operation_not_ready:
      return "operation not yet finisehd or would block";
  }

  return "(unrecognized condition)";
}
}  // namespace olifilo
