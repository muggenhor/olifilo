// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/io/errors.hpp>

namespace olifilo::io
{
const char* error_category_t::name() const noexcept
{
  return "io-error";
}

std::string error_category_t::message(int ev) const
{
  switch (static_cast<error>(ev))
  {
    case error::operation_not_ready:
      return "operation not yet finisehd or would block";
  }

  return "(unrecognized condition)";
}
}  // namespace olifilo::io
