// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <system_error>

#include "forward.hpp"

namespace olifilo::detail
{
// used to register coroutines waiting for events and wait for all those events
class io_poll_context
{
  public:
    std::error_code operator()(promise_wait_callgraph& polled);
};
}  // namespace olifilo::detail
