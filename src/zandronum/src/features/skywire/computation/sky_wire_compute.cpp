// [rc4l] See sky_wire_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "sky_wire_compute.h"

namespace zx
{

const char *SkyNameForWire(const char *name)
{
	return name != nullptr ? name : "";
}

} // namespace zx
