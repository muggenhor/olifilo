// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "file_descriptor.hpp"

namespace olifilo::io
{
class socket_descriptor : public file_descriptor
{
  public:
    using file_descriptor::file_descriptor;
};
}  // olifilo::io
