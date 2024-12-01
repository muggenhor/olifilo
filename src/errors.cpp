// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/errors.hpp>

namespace olifilo
{
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
