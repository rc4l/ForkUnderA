// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/compat-oldnodelay/computation/oldnodelay_compute.h"

namespace zx { namespace compat {

int ComputeOldNoDelayTics(int startTics, int tics)
{
	return (startTics > 0 && tics >= 0) ? tics + 1 : tics;
}

}} // namespace zx::compat
